#include <pthread.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <semaphore.h>
#include "cacti.h"

#define INITIAL_SIZE 32

void exit_program(const char *err) {
    printf("%s\n", err);
    exit(10);
}

struct QueueOfMessages {
    long front;
    long end;
    long size;
    long max_size;
    message_t *messages;
};

int init_queue_of_messages(struct QueueOfMessages *queue) {
    queue->front = 0;
    queue->end = 0;
    queue->size = 0;
    queue->max_size = ACTOR_QUEUE_LIMIT;
    queue->messages = malloc(ACTOR_QUEUE_LIMIT * sizeof(message_t));

    if (!queue->messages)
        return 1;

    return 0;
}

struct Actor {
    actor_id_t id;
    role_t *role;
    bool is_dead;
    void *state_ptr;
    struct QueueOfMessages queue;
};

int init_actor(struct Actor *actor) {
    actor->state_ptr = NULL;
    actor->is_dead = false;
    actor->id = 0;
    if (init_queue_of_messages(&actor->queue) != 0)
        return 1;
    return 0;
}

void destroy_actor(struct Actor *actor) {
    free(actor->queue.messages);
    free(actor);
}

struct QueueOfActors {
    long front;
    long end;
    long size;
    long max_size;
    actor_id_t *ids;
};

struct State {
    long number_of_active_actors;
    long number_of_all_actors;
    long max_number_of_actors;

    struct Actor **actors;
    pthread_t *threads_ids;
    short *actor_state;

    pthread_cond_t end_of_life_cond;
    pthread_mutex_t main_lock;
    sem_t queue_not_empty;

    bool end_of_life;
    bool is_interrupted;
    bool valid;

    pthread_key_t thread_key;
    pthread_attr_t attr;

    sigset_t block_mask;

    struct QueueOfActors queue;

};

struct State global_state_instance;
struct State *global_state = &global_state_instance;

int init_queue_of_actors(struct QueueOfActors *queue) {
    queue->front = 0;
    queue->end = 0;
    queue->size = 0;
    queue->max_size = INITIAL_SIZE;
    queue->ids = calloc(INITIAL_SIZE, sizeof(actor_id_t));

    if (!queue->ids)
        return 1;

    return 0;
}

void destroy_actors(unsigned long how_many) {
    for (unsigned long i = 0; i < how_many; i++)
        destroy_actor(global_state->actors[i]);
}

void destroy_state(struct State *state) {
    if (pthread_attr_destroy(&state->attr) != 0)
        exit_program("actor_system_join; attr destroy failed");

    if (pthread_mutex_destroy(&state->main_lock) != 0)
        exit_program("actor_system_join; pthread_mutex_destroy");

    if (pthread_cond_destroy(&state->end_of_life_cond) != 0)
        exit_program("actor_system_join; pthread_cond_destroy");

    if (pthread_key_delete(state->thread_key) != 0)
        exit_program("actor_system_join; pthread_key_delete");

    if (sem_destroy(&state->queue_not_empty) != 0)
        exit_program("actor_system_join; sem_destroy");

    free(state->queue.ids);
    destroy_actors(state->number_of_all_actors);
    free(state->actor_state);
    free(state->actors);
    free(state->threads_ids);

    state->valid = false;
}

void add_actor_to_queue_of_actors(actor_id_t actor) {
    global_state->queue.ids[global_state->queue.end] = actor;
    global_state->queue.end = (global_state->queue.end + 1) % (global_state->queue.max_size);
    global_state->queue.size += 1;
}

void double_queue_of_actors_size() {
    global_state->queue.ids =
            realloc(
                    global_state->queue.ids,
                    2 * global_state->queue.max_size * sizeof(actor_id_t)
            );


    if (!global_state->queue.ids)
        exit_program("double_queue_of_actors_size(): realloc failed");
}

void move_values_in_queue_of_actors() {
    for (int i = 0; i < global_state->queue.end; i++)
        global_state->queue.ids[global_state->queue.size + i] = global_state->queue.ids[i];

    global_state->queue.end = global_state->queue.size + global_state->queue.end;
}


actor_id_t pop_queue_of_actor_ids() {
    actor_id_t ans = global_state->queue.ids[global_state->queue.front];
    global_state->queue.front = (global_state->queue.front + 1) % (global_state->queue.max_size);
    global_state->queue.size -= 1;

    return ans;
}

void push_queue_of_actors(actor_id_t actor) {
    if (global_state->queue.size == global_state->queue.max_size) {
        double_queue_of_actors_size();
        move_values_in_queue_of_actors();
        global_state->queue.max_size *= 2;
    }

    add_actor_to_queue_of_actors(actor);
}

message_t pop_queue_of_messages(struct Actor *actor) {
    if (actor->queue.size == 0)
        exit_program("pop_queue_of_messages(): no messages");

    message_t ans = actor->queue.messages[actor->queue.front];
    actor->queue.front = (actor->queue.front + 1) % (actor->queue.max_size);
    actor->queue.size -= 1;

    return ans;
}


int push_queue_of_messages(struct Actor *actor, message_t message) {
    if (actor->queue.size == ACTOR_QUEUE_LIMIT)
        return 1;

    actor->queue.messages[actor->queue.end] = message;
    actor->queue.end = (actor->queue.end + 1) % (actor->queue.max_size);
    actor->queue.size += 1;

    return 0;
}

void get_main_lock() {
    if (pthread_mutex_lock(&global_state->main_lock) != 0)
        exit(10);
}

void release_main_lock() {
    if (pthread_mutex_unlock(&global_state->main_lock) != 0)
        exit(10);
}

void double_array_of_actors_size() {
    global_state->actors =
            realloc(global_state->actors, 2 * global_state->max_number_of_actors * sizeof(struct Actor *));

    global_state->actor_state =
            realloc(global_state->actor_state, 2 * global_state->max_number_of_actors * sizeof(short));

    if (!global_state->actors)
        exit_program("double_array_of_actors_size; realloc");

    if (!global_state->actor_state)
        exit_program("double_array_of_actors_size; realloc");

    global_state->max_number_of_actors *= 2;
}

actor_id_t create_new_actor(void *role) {
    if (global_state->number_of_all_actors == global_state->max_number_of_actors)
        double_array_of_actors_size();

    actor_id_t new_actor_id = global_state->number_of_all_actors;

    global_state->number_of_all_actors += 1;
    global_state->number_of_active_actors += 1;
    global_state->actors[new_actor_id] = malloc(sizeof(struct Actor));

    if (!global_state->actors[new_actor_id])
        exit_program("create_new_actor; malloc");

    if (init_actor(global_state->actors[new_actor_id]) != 0)
        exit_program("init_actor; alloc");

    global_state->actors[new_actor_id]->role = role;
    global_state->actors[new_actor_id]->id = new_actor_id;

    return new_actor_id;
}

void send_hello_message(const actor_id_t send_from, actor_id_t send_to) {
    message_t hello_message = {
            .message_type = MSG_HELLO,
            .nbytes = sizeof(actor_id_t),
            .data = (void *) send_from
    };

    struct Actor *my_actor = global_state->actors[send_to];

    push_queue_of_messages(my_actor, hello_message);

    push_queue_of_actors(send_to);
    global_state->actor_state[send_to] = 2;
    sem_post(&global_state->queue_not_empty);
}

void check_nprompts_index(struct Actor *my_actor, size_t index) {
    if (my_actor->role->nprompts < index)
        exit_program("check_nprompts_index; out of bound");
}


int send_message(actor_id_t send_to, message_t message) {
    get_main_lock();
    if (send_to >= global_state->number_of_all_actors) {
        release_main_lock();
        return -2;
    }

    struct Actor *my_actor = global_state->actors[send_to];

    if (global_state->is_interrupted) {
        release_main_lock();
        return -1;
    }

    if (my_actor->is_dead) {
        release_main_lock();
        return -1;
    }

    if (push_queue_of_messages(my_actor, message) != 0) {
        release_main_lock();
        return -3;
    }

    if (global_state->actor_state[send_to] == 0) {
        push_queue_of_actors(send_to);
        global_state->actor_state[send_to] = 2;
        sem_post(&global_state->queue_not_empty);
    }

    release_main_lock();

    return 0;
}

bool handle_message_of_actor(struct Actor *my_actor, message_t message) {
    if (message.message_type == MSG_SPAWN) {
        if (global_state->is_interrupted)
            return false;

        //undefined
        if (global_state->number_of_all_actors == CAST_LIMIT)
            return false;

        actor_id_t new_actor_id = create_new_actor(message.data);
        send_hello_message(my_actor->id, new_actor_id);
        return false;
    }

    if (message.message_type == MSG_GODIE) {
        if (!my_actor-> is_dead) {
            my_actor->is_dead = true;
            global_state->number_of_active_actors -= 1;

            if (global_state->number_of_active_actors == 0) {
                global_state->end_of_life = true;

                if (pthread_cond_signal(&global_state->end_of_life_cond) != 0)
                    exit_program("actor_controller; cond_signal_failed");
            }
        }

        return false;
    }

    release_main_lock();

    check_nprompts_index(my_actor, message.message_type);
    my_actor->role->prompts[message.message_type](
            &my_actor->state_ptr,
            message.nbytes,
            message.data
    );

    return true;
}

void *actor_controller() {
    while (1) {
        sem_wait(&global_state->queue_not_empty);
        get_main_lock();

        if (global_state->end_of_life == true) {
            release_main_lock();
            break;
        }

        actor_id_t my_actor_id = pop_queue_of_actor_ids();
        global_state->actor_state[my_actor_id] = 1;
        struct Actor *my_actor = global_state->actors[my_actor_id];
        pthread_setspecific(global_state->thread_key, (void *) my_actor->id);
        message_t my_message = pop_queue_of_messages(my_actor);

        bool lock_dropped = handle_message_of_actor(my_actor, my_message);

        if (lock_dropped)
            get_main_lock();

        if (my_actor->queue.size > 0) {
            push_queue_of_actors(my_actor->id);
            global_state->actor_state[my_actor_id] = 2;
            sem_post(&global_state->queue_not_empty);
        }
        else {
            if (my_actor -> is_dead == false && global_state -> is_interrupted) {
                my_actor->is_dead = true;
                global_state->number_of_active_actors -= 1;

                if (global_state->number_of_active_actors == 0) {
                    global_state->end_of_life = true;

                    if (pthread_cond_signal(&global_state->end_of_life_cond) != 0)
                        exit_program("actor_controller; cond_signal_failed");
                }

            }

            global_state->actor_state[my_actor_id] = 0;
        }

        release_main_lock();
    }

    return 0;
}


void handle_sigint() {
    get_main_lock();

    global_state->is_interrupted = true;

    for (long i = 0; i < global_state->number_of_active_actors; i++) {
        if (global_state -> actors[i] -> is_dead == false &&
            global_state -> actors[i] -> queue.size == 0 &&
            global_state -> actor_state[i] == 0) {
            global_state->number_of_active_actors -= 1;

            if (global_state->number_of_active_actors == 0) {
                global_state -> end_of_life = true;

                if (pthread_cond_signal(&global_state->end_of_life_cond) != 0)
                    exit_program("actor_controller; cond_signal_failed");
            }
        }
    }

    release_main_lock();
}

void *sigint_handler_instructions() {
    int old_type;

    if (pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &old_type) != 0)
        exit_program("thread_signal_handler; pthread_setcanceltype");

    struct sigaction sigint_handling;
    sigint_handling.sa_handler = handle_sigint;

    if (sigemptyset(&sigint_handling.sa_mask) != 0)
        exit_program("thread_signal_handler; sigemptyset");

    sigint_handling.sa_flags = 0;
    sigaction(SIGINT, &sigint_handling, NULL);

    int sig;
    sigwait(&sigint_handling.sa_mask, &sig);

    return 0;
}


int init_worker_threads() {
    for (int i = 0; i < POOL_SIZE; i++)
        if (pthread_create(&(global_state->threads_ids[i]), &global_state->attr, actor_controller, NULL) != 0)
            return 1;

    return 0;
}


int init_signal_thread() {
    if (pthread_create(&(global_state->threads_ids[POOL_SIZE]), &global_state->attr, sigint_handler_instructions, NULL) != 0)
        return 1;

    return 0;
}


bool init_state() {
    if (pthread_attr_init(&global_state->attr) != 0)
        return false;

    if (pthread_attr_setdetachstate(&global_state->attr, PTHREAD_CREATE_JOINABLE) != 0)
        return false;

    global_state->threads_ids = malloc((POOL_SIZE + 1) * sizeof(pthread_t));

    if (!global_state->threads_ids)
        return false;

    if (init_signal_thread() != 0)
        return false;

    if (sigemptyset(&global_state->block_mask) != 0)
        return false;

    if (sigfillset(&global_state->block_mask) != 0)
        return false;

    sigset_t old_mask;

    if (pthread_sigmask(SIG_BLOCK, &global_state->block_mask, &old_mask) != 0)
        return false;

    if (pthread_key_create(&global_state->thread_key, NULL) != 0)
        return false;

    if (init_queue_of_actors(&global_state->queue) != 0)
        return false;

    if (pthread_mutex_init(&global_state->main_lock, NULL) != 0)
        return false;

    global_state->number_of_all_actors = 0;
    global_state->number_of_active_actors = 0;
    global_state->max_number_of_actors = INITIAL_SIZE;
    global_state->end_of_life = false;
    global_state->is_interrupted = false;
    global_state->valid = true;

    global_state->actors = malloc(INITIAL_SIZE * sizeof(struct Actor *));

    if (!global_state->actors)
        return false;

    global_state->actor_state = calloc(INITIAL_SIZE, sizeof(short));

    if (!global_state->actor_state)
        return false;

    if (pthread_cond_init(&global_state->end_of_life_cond, NULL) != 0)
        return false;

    if (sem_init(&global_state->queue_not_empty, 0, 0) != 0)
        return false;

    if (init_worker_threads() != 0)
        return false;

    if (pthread_sigmask(SIG_SETMASK, &old_mask, NULL) != 0)
        return false;

    return true;
}

int actor_system_create(actor_id_t *actor, role_t *const role) {
    if (!init_state())
        return -1;
    *actor = 0;

    get_main_lock();

    actor_id_t new_actor_id = create_new_actor((void *) role);
    send_hello_message(0, new_actor_id);

    release_main_lock();

    return 0;
}

void actor_system_join(actor_id_t actor) {
    if (!global_state->valid)
        return;

    get_main_lock();

    if (global_state->number_of_all_actors <= actor) {
        release_main_lock();
        return;
    }

    while (global_state->end_of_life == false)
        pthread_cond_wait(&global_state->end_of_life_cond, &global_state->main_lock);

    for (int i = 0; i < POOL_SIZE; i++)
        sem_post(&global_state->queue_not_empty);

    release_main_lock();

    for (size_t i = 0; i < POOL_SIZE; i++) 
        if (pthread_join(global_state->threads_ids[i], NULL) != 0)
            exit_program("actor_system_join; pthread_join");

    if (pthread_cancel(global_state->threads_ids[POOL_SIZE]) != 0)
        exit_program("actor_system_join; pthread_cancel");

    if (pthread_join(global_state->threads_ids[POOL_SIZE], NULL) != 0)
        exit_program("actor_system_join; pthread_join");

    destroy_state(global_state);
}

actor_id_t actor_id_self() {
    return (actor_id_t) pthread_getspecific(global_state->thread_key);
}

