#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include "cacti.h"

#define ignore (void)

int dummy_count = 0;

void hello_from_the_other_side(void **stateptr, size_t nbytes, void *data) {
    ignore stateptr;
    ignore nbytes;
    printf("%ld: Hello, it's me and %ld is my parent\n", actor_id_self(), (actor_id_t) data);
}

act_t  adele_prompts[] = {
        hello_from_the_other_side
};
role_t adele_role      = {
        .nprompts = 1,
        .prompts = adele_prompts
};

void hello_recursion(void **stateptr, size_t nbytes, void *data) {
    ignore stateptr;
    ignore nbytes;
    ignore data;
    sleep(1);
    printf("%ld: No condom today!\n", actor_id_self());
    send_message(actor_id_self(), (message_t) {
            .message_type = MSG_SPAWN,
            .nbytes = sizeof(adele_role),
            .data = &adele_role
    });
    send_message(actor_id_self(), (message_t) {
            .message_type = MSG_HELLO
    });
}

act_t  isaac_prompts[] = {
        hello_recursion
};
role_t isaac_role      = {
        .nprompts = 1,
        .prompts = isaac_prompts
};
int    isaac_lives     = 0;

void dummy_printer(void **stateptr, size_t nbytes, void *data) {
    ignore stateptr;
    ignore nbytes;
    ignore data;
    if (!isaac_lives) {
        isaac_lives = 1;
        printf("%ld: Spawning isaac to populate the world...\n", actor_id_self());
        send_message(actor_id_self(), (message_t) {
                .message_type = MSG_SPAWN,
                .nbytes = sizeof(isaac_role),
                .data  = &isaac_role
        });
    } else
        printf("%ld: DUMMY COUNT = %d\n", actor_id_self(), ++dummy_count);
    sleep(1);
}

act_t  dummy_prompts[] = {
        dummy_printer
};
role_t dummy_role      = {
        .nprompts = 1,
        .prompts = dummy_prompts
};

int main() {
    actor_id_t id;
    actor_system_create(&id, &dummy_role);
    sleep(1);
    for (int i = 0; i < 20; ++i) {
        send_message(id, (message_t) {
                .message_type = MSG_HELLO
        });
    }
    printf("Press Ctrl+C before 20 seconds pass. After that no Adele should be spawned and DUMMY COUNT should get to 20\n");
    actor_system_join(id);
    assert(dummy_count == 20);
    return 0;
}