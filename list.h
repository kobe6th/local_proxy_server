#ifndef LIST_H_
#define LIST_H_

#define __INLINE__ inline

typedef struct list_head list_node;

struct list_head {
    list_node *next, *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define LIST_HEAD(name) \
    list_node name = LIST_HEAD_INIT(name)

#define INIT_LIST_HEAD(ptr) do { \
    (ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

#define list_entry(ptr, type, member) \
    ((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

static __INLINE__ void __list_add(list_node *new,
                  list_node *prev,
                  list_node *next)
{
    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

static __INLINE__ void list_add(list_node *new, list_node *head)
{
    __list_add(new, head, head->next);
}

static __INLINE__ void list_add_tail(list_node *new, list_node *head)
{
    __list_add(new, head->prev, head);
}

static __INLINE__ void __list_del(list_node *prev, list_node *next)
{
    next->prev = prev;
    prev->next = next;
}

static __INLINE__ void list_del(list_node *entry)
{
    __list_del(entry->prev, entry->next);
    entry->next = (void *) 0;
    entry->prev = (void *) 0;
}

static __INLINE__ void list_del_init(list_node *entry)
{
    __list_del(entry->prev, entry->next);
    INIT_LIST_HEAD(entry); 
}

static __INLINE__ void list_move(list_node *list, list_node *head)
{
        __list_del(list->prev, list->next);
        list_add(list, head);
}

static __INLINE__ void list_move_tail(list_node *list,
                  list_node *head)
{
        __list_del(list->prev, list->next);
        list_add_tail(list, head);
}

static __INLINE__ int list_empty(list_node *head)
{
    return head->next == head;
}

#endif /* LIST_H_ */

