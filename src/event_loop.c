static void *event_loop_run(void *context)
{
    struct queue_item *head;
    struct event_loop *event_loop = (struct event_loop *) context;

    while (event_loop->is_running) {
        do {
            head = event_loop->head;
            if (!head->next) break;
        } while (!__sync_bool_compare_and_swap(&event_loop->head, head, head->next));

        if (head->next) {
            struct event *event = (void *)head->next + sizeof(struct queue_item);

            uint32_t result = event_handler[event->type](event->context, event->param1);
            if (event->info) *event->info = (result << 0x1) | EVENT_PROCESSED;

            event_signal_flush();
            ts_reset();
        } else {
            sem_wait(event_loop->semaphore);
        }
    }

    return NULL;
}

void event_loop_post(struct event_loop *event_loop, enum event_type type, void *context, int param1, volatile uint32_t *info)
{
    assert(event_loop->is_running);

    bool success;
    struct event *event;
    struct queue_item *tail, *new_tail;

    new_tail = memory_pool_push(&event_loop->pool, sizeof(struct queue_item) + sizeof(struct event));
    new_tail->next = NULL;

    event = (void *)new_tail + sizeof(struct queue_item);
    event->type = type;
    event->context = context;
    event->param1 = param1;
    event->info = info;

    __asm__ __volatile__ ("" ::: "memory");

    do {
        tail = event_loop->tail;
        success = __sync_bool_compare_and_swap(&tail->next, NULL, new_tail);
    } while (!success);
    __sync_bool_compare_and_swap(&event_loop->tail, tail, new_tail);

    sem_post(event_loop->semaphore);
}

bool event_loop_init(struct event_loop *event_loop)
{
    if (!memory_pool_init(&event_loop->pool, KILOBYTES(128))) return false;

    event_loop->head = memory_pool_push(&event_loop->pool, sizeof(struct queue_item) + sizeof(struct event));
    event_loop->head->next = NULL;
    event_loop->tail = event_loop->head;

    event_loop->is_running = false;
    event_loop->semaphore = sem_open("yabai_event_loop_semaphore", O_CREAT, 0600, 0);
    sem_unlink("yabai_event_loop_semaphore");

    return event_loop->semaphore != SEM_FAILED;
}

bool event_loop_begin(struct event_loop *event_loop)
{
    if (event_loop->is_running) return false;
    event_loop->is_running = true;
    pthread_create(&event_loop->thread, NULL, &event_loop_run, event_loop);
    return true;
}

bool event_loop_end(struct event_loop *event_loop)
{
    if (!event_loop->is_running) return false;
    event_loop->is_running = false;
    pthread_join(event_loop->thread, NULL);
    return true;
}
