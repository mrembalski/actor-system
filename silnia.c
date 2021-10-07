#include <stdlib.h>
#include <stdio.h>
#include "cacti.h"

#define MSG_IDENTIFY_SON (message_type_t)    0x1
#define MSG_FACTORIAL  (message_type_t)     0x2

#ifdef __GNUC__
#  define UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#else
#  define UNUSED(x) UNUSED_ ## x
#endif
typedef unsigned long long ull;

void first_actor_hello          (void **stateptr, size_t nbytes, void *data);
void other_actors_hello         (void **stateptr, size_t nbytes, void *data);
void calculate_factorial        (void **stateptr, size_t nbytes, void *data);
void receive_son_id             (void **stateptr, size_t nbytes, void *data);
typedef void (*actor_functions) (void**, size_t, void*);

actor_functions first_actor_functions[3]    = { &first_actor_hello, &receive_son_id, &calculate_factorial };
actor_functions other_actors_functions[3]   = { &other_actors_hello, &receive_son_id, &calculate_factorial };
role_t first_actor_role                     = { .nprompts = 3, .prompts = first_actor_functions };
role_t other_actors_role                    = { .nprompts = 3, .prompts = other_actors_functions };

typedef struct comm_info {
    ull factorial;
    ull step;
    ull n;
} comm_info_t;

typedef struct actor_info {
    comm_info_t* values;
    actor_id_t my_son_id;
} actor_info_t;

void kill_myself() {
    message_t commit_suicide = {
            .message_type = MSG_GODIE,
            .nbytes = 0,
            .data = NULL
    };

    send_message(actor_id_self(), commit_suicide);
}

void first_actor_hello (void **stateptr, size_t UNUSED(nbytes), void *UNUSED(data)) {
    (*stateptr) = malloc(sizeof(actor_info_t));

    ((actor_info_t*) (*stateptr)) -> my_son_id      = 0;
}

void other_actors_hello (void **stateptr, size_t UNUSED(nbytes), void *data) {
    (*stateptr) = malloc(sizeof(actor_info_t));

    ((actor_info_t*) (*stateptr)) -> my_son_id      = 0;

    message_t identify_son = {
            .message_type   = MSG_IDENTIFY_SON,
            .nbytes         = sizeof (actor_id_t),
            .data           = (void*) actor_id_self()
    };

    send_message((actor_id_t) data, identify_son);
}

void receive_son_id (void **stateptr, size_t UNUSED(nbytes), void *data) {
    ((actor_info_t*) (*stateptr)) -> my_son_id = (actor_id_t) data;

    message_t calculate = {
            .message_type   = MSG_FACTORIAL,
            .nbytes         = sizeof (comm_info_t *),
            .data           = (void *) ((actor_info_t*) (*stateptr)) -> values
    };

    send_message(((actor_info_t*) (*stateptr)) -> my_son_id, calculate);

    free(*stateptr);
    kill_myself();
}


void calculate_factorial (void **stateptr, size_t UNUSED(nbytes), void *data) {
    ((actor_info_t*) (*stateptr)) -> values = (comm_info_t*) data;

    if (((comm_info_t*) (data)) -> step == ((comm_info_t*) (data)) -> n) {
        if (((comm_info_t*) (data)) -> step == 0)
            printf("1\n");
        else
            printf("%llu\n", ((comm_info_t*) (data)) -> factorial * ((comm_info_t*) (data)) -> step);

        free(((comm_info_t*) (data)));
        free(*stateptr);
        kill_myself();

        return;
    }

    if (((comm_info_t*) (data)) -> step != 0)
        ((comm_info_t*) (data)) -> factorial *= ((comm_info_t*) (data)) -> step;

    ((comm_info_t*) (data)) -> step += 1;

    message_t spawn_next = {
            .message_type = MSG_SPAWN,
            .nbytes = sizeof(role_t*),
            .data = &other_actors_role
    };

    send_message(actor_id_self(), spawn_next);
}


ull n;
int main() {
    scanf("%llu", &n);

    actor_id_t first_actor_id;

    comm_info_t* starter = malloc(sizeof(comm_info_t));

    starter -> factorial    = 1;
    starter -> step         = 0;
    starter -> n            = n;


    if (actor_system_create(&first_actor_id, &first_actor_role) == 0) {
        message_t first_message =  {
                .message_type = MSG_FACTORIAL,
                .nbytes = sizeof(void *),
                .data = starter,
        };

        send_message(first_actor_id, first_message);
    }

    actor_system_join(first_actor_id);

    return 0;
}