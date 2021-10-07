#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "cacti.h"

#define MSG_IDENTIFY_SON (message_type_t)   0x1
#define MSG_CREATE_COLUMN  (message_type_t) 0x2
#define MSG_CREATE_COLUMN_END  (message_type_t) 0x3
#define MSG_COUNT_ROW   (message_type_t) 0x4

#ifdef __GNUC__
#  define UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#else
#  define UNUSED(x) UNUSED_ ## x
#endif


typedef unsigned long long ull;

void first_actor_hello          (void **stateptr, size_t nbytes, void *data);//0
void other_actors_hello         (void **stateptr, size_t nbytes, void *data);//0
void receive_son_id             (void **stateptr, size_t nbytes, void *data);//1
void create_column              (void **stateptr, size_t nbytes, void *data);//2
void create_column_end          (void **stateptr, size_t nbytes, void *data);//3
void count_row                  (void **stateptr, size_t nbytes, void *data);//4

typedef void (*actor_functions) (void**, size_t, void*);

actor_functions first_actor_functions[5]    = { &first_actor_hello, &receive_son_id, &create_column, &create_column_end, &count_row };
actor_functions other_actors_functions[5]   = { &other_actors_hello, &receive_son_id, &create_column, &create_column_end, &count_row };
role_t first_actor_role                     = { .nprompts = 5, .prompts = first_actor_functions };
role_t other_actors_role                    = { .nprompts = 5, .prompts = other_actors_functions };

typedef struct init_column_info {
    ull step;
    ull number_of_columns;
    ull number_of_rows;

    ull *values;
    ull *times;

    //only the last actor (last column) will edit it
    ull *final_sums;

    actor_id_t first_actor_id;
} init_column_info_t;

typedef struct actor_info {
    init_column_info_t* init_column_values;
    actor_id_t my_son_id;
    ull my_column;
} actor_info_t;

typedef struct row_info {
    ull row_number;
    ull sum_of_row;
} row_info_t;


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
    actor_info_t *my_memory = ((actor_info_t*) (*stateptr));

    my_memory -> my_son_id      = 0;
    my_memory -> my_column      = 0;
}

void other_actors_hello (void **stateptr, size_t UNUSED(nbytes), void *data) {
    (*stateptr) = malloc(sizeof(actor_info_t));
    actor_info_t *my_memory = ((actor_info_t*) (*stateptr));

    my_memory -> my_son_id      = 0;
    my_memory -> my_column      = 0;

    message_t identify_son = {
            .message_type   = MSG_IDENTIFY_SON,
            .nbytes         = sizeof (actor_id_t),
            .data           = (void*) actor_id_self()
    };

    send_message((actor_id_t) data, identify_son);
}

void receive_son_id (void **stateptr, size_t UNUSED(nbytes), void *data) {
    actor_info_t *my_memory = ((actor_info_t*) (*stateptr));

    my_memory -> my_son_id = (actor_id_t) data;

    message_t calculate = {
            .message_type   = MSG_CREATE_COLUMN,
            .nbytes         = sizeof (init_column_info_t *),
            .data           = (void *) my_memory -> init_column_values
    };

    send_message(my_memory -> my_son_id, calculate);
}


void create_column (void **stateptr, size_t UNUSED(nbytes), void *data) {
    actor_info_t *my_memory         = ((actor_info_t*) (*stateptr));
    init_column_info_t *my_data     = ((init_column_info_t*) (data));

    my_memory -> init_column_values = my_data;
    my_memory -> my_column          = my_data -> step;

    if (my_memory -> my_column == my_data -> number_of_columns - 1) {

        message_t end_of_column_creation_message = {
                .message_type = MSG_CREATE_COLUMN_END,
                .nbytes = 0,
                .data = NULL
        };

        send_message(my_data -> first_actor_id, end_of_column_creation_message);
        return;
    }

    my_data -> step += 1;

    message_t spawn_next = {
            .message_type = MSG_SPAWN,
            .nbytes = sizeof(role_t*),
            .data = &other_actors_role
    };

    send_message(actor_id_self(), spawn_next);
}


void create_column_end (void **stateptr, size_t UNUSED(nbytes), void *UNUSED(data)) {
    actor_info_t *my_memory         = ((actor_info_t*) (*stateptr));

    for (ull i = 0; i < my_memory -> init_column_values -> number_of_rows; i++) {
        row_info_t *row_info = malloc(sizeof(row_info_t));

        row_info -> sum_of_row = 0;
        row_info -> row_number = i;

        message_t count_row_message = {
                .message_type = MSG_COUNT_ROW,
                .nbytes = sizeof(row_info_t *),
                .data = (void *) row_info
        };

        send_message(actor_id_self(), count_row_message);
    }
}

void count_row (void **stateptr, size_t UNUSED(nbytes), void *data) {
    actor_info_t *my_memory         = ((actor_info_t*) (*stateptr));
    row_info_t *row_info            = (row_info_t*) data;
    ull my_index                    = row_info -> row_number * my_memory -> init_column_values -> number_of_columns + my_memory -> my_column;

    usleep(my_memory -> init_column_values -> times [my_index] * 1000);

    if (my_memory -> my_column == my_memory -> init_column_values -> number_of_columns - 1) {

        my_memory -> init_column_values -> final_sums[my_index / my_memory -> init_column_values -> number_of_columns] =
                my_memory -> init_column_values -> values [my_index] + row_info -> sum_of_row;


        if (row_info -> row_number == my_memory -> init_column_values -> number_of_rows - 1) {
            free(*stateptr);
            kill_myself();
        }

        free(row_info);
        return;
    }

    row_info -> sum_of_row += my_memory -> init_column_values -> values [my_index];

    message_t count_row_message = {
            .message_type = MSG_COUNT_ROW,
            .nbytes = sizeof(row_info_t *),
            .data = (void *) row_info
    };

    send_message(my_memory -> my_son_id, count_row_message);

    if (row_info -> row_number == my_memory -> init_column_values -> number_of_rows - 1) {
        free(*stateptr);
        kill_myself();
    }
}


ull k, n;

//k - wiersze
//n - kolumny

int main() {
    actor_id_t first_actor_id;

    if (actor_system_create(&first_actor_id, &first_actor_role) != 0)
        return -1;

    scanf("%llu%llu", &k, &n);

    ull *values = malloc(k * n * sizeof(ull));
    ull *times = malloc(k * n * sizeof(ull));
    ull *final_sums = malloc(k * sizeof(ull));

    for (ull i = 0; i < k * n; i++)
        scanf("%lld%lld", &values[i], &times[i]);

    init_column_info_t *starter = malloc(sizeof(init_column_info_t));

    starter->step = 0;
    starter->times = times;
    starter->values = values;
    starter->number_of_columns = n;
    starter->number_of_rows = k;
    starter->first_actor_id = first_actor_id;
    starter->final_sums = final_sums;

    message_t first_message = {
            .message_type = MSG_CREATE_COLUMN,
            .nbytes = sizeof(void *),
            .data = starter,
    };

    send_message(first_actor_id, first_message);

    actor_system_join(first_actor_id);

    for (ull j = 0; j < k; j++)
        printf("%llu\n", final_sums[j]);

    free(starter);
    free(values);
    free(times);
    free(final_sums);
}