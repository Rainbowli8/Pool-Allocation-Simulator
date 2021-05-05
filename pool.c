#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "cs136-trace.h"
#include "array-tools.h"
#include "pool.h"

const int NONE = -1;

struct llnode {
  int size;
  char *addr;       // addr of p->data[i]
  int start;
  int end;
  struct llnode *next;
};

struct llist {
  struct llnode *front;
  struct llnode *back;
  int len;
};

struct pool {
  int active_alloc;
  char *data;
  struct llist *used;
  struct llist *avail;
};

struct pool *pool_create(int size) {
  assert(size > 0);
  struct pool *p = malloc(sizeof(struct pool));
  p->active_alloc = 0;
  p->data = malloc(size * sizeof(char));
  p->used = malloc(sizeof(struct llist));
  p->used->front = NULL;
  p->used->back = NULL;
  p->used->len = 0;
  p->avail = malloc(sizeof(struct llist));
  struct llnode *node = malloc(sizeof(struct llnode));
  node->size = size;
  node->addr = &p->data[0];
  node->start = 0;
  node->end = size;
  node->next = NULL;
  p->avail->front = node;
  p->avail->back = node;
  p->avail->len = 1;
  return p;
}

// at this point, p->avail should have 1 element and p->used is NULL
bool pool_destroy(struct pool *p) {
  assert(p);
  if (p->active_alloc == 0) {
    free(p->avail->front);
    free(p->avail);
    free(p->used->front);
    free(p->used);
    free(p->data);
    free(p);
    return true;
  } else {
    return false; 
  }
}

///////////////////////////////////////////////////////////////////////////
// LINKED LIST OPERATIONS:
// time: O(1) :  remove_front, remove_back, add_front
// time: O(n) where n is len of list : remove_node, add_node
// remove_front(lst) removes the first node in lst
// effects: modifies lst
static void remove_front(struct llist *lst) {
  assert(lst->front);
  struct llnode *old_front = lst->front;
  lst->front = lst->front->next;
  if (lst->front == NULL) {
    // lst is now empty
    lst->back = NULL;
  }
  free(old_front);
  lst->len--;
}

// remove_back(lst) removes the last node in lst
// effects: modifies lst
static void remove_back(struct llist *lst) {
  assert(lst->back);
  struct llnode *old_back = lst->back;
  struct llnode *node = lst->front;
  int count = 0;
  int idx = lst->len - 2;             // find second last node
  while (count < idx) {
    count++;
    node = node->next;
  }
  node->next = NULL;
  lst->back = node;
  free(old_back);
  lst->len--;
}

// remove_node(lst, s, size, start) removes a node with the parameters and
//   returns true if successful (fails if s DNE in lst)
// effects: may modify lst
static bool remove_node(struct llist *lst, char *s, int *size, int *start) {
  if (!lst->front) {
    return false;
  }
  struct llnode *before = lst->front;
  if (lst->front->addr == s) {        // remove first item
    *size = lst->front->size;
    *start = lst->front->start;
    remove_front(lst);
    return true;
  } else if (lst->back->addr == s) {  // remove last item
    *size = lst->back->size;
    *start = lst->back->start;
    remove_back(lst);
    return true;
  } else {
    while (before->next && before->next->addr != s) {
      before = before->next;
    }
    if (before->next != NULL) {
      struct llnode *old_node = before->next;
      *size = old_node->size;
      *start = old_node->start;
      before->next = before->next->next;
      free(old_node);
      lst->len--;
      return true;
    } else {
      return false; 
    }
  }
}

// add_front(lst, size, addr, start) adds a node with information to the
//   front of lst
// effects: modifies lst
static void add_front(struct llist *lst, int size, char *addr, int start) {
  struct llnode *newnode = malloc(sizeof(struct llnode));
  if (lst->front) {            // not empty
    newnode->addr = addr;
    newnode->size = size;
    newnode->start = start;
    newnode->end = start + size;
    newnode->next = lst->front;
    lst->front = newnode;
  } else {                     // empty
    newnode->addr = addr;
    newnode->size = size;
    newnode->start = start;
    newnode->end = start + size;
    newnode->next = lst->front;
    lst->front = newnode;
    lst->back = lst->front;
  }
  lst->len++;
}

// add_node(lst, size, addr, start) adds a node with information to lst
// effects: modifies lst
static void add_node(struct llist *lst, char *addr, int size, int start) {
  if (lst->front == NULL || start < lst->front->start) {
    add_front(lst, size, addr, start);
  } else {
    // find the node that will be "before" our insert
    struct llnode *before = lst->front;
    while (before->next && start > before->next->start) {
      before = before->next;
    }
    // now do the insert
    struct llnode *newnode = malloc(sizeof(struct llnode));
    newnode->addr = addr;
    newnode->size = size;
    newnode->start = start;
    newnode->end = start + size;
    newnode->next = before->next;
    before->next = newnode;
    if (newnode->next == NULL) {     // if new node is at the back
      lst->back = newnode;
    }
    lst->len++;
  }
}
///////////////////////////////////////////////////////////////////////////
// get_available(p, size, start) modifies p by removing a node (in
//   p->avail) and returns the pointer of the first address in p->avail
//   such that a dynamic array of size can be allocated, and updates start
//   to contain the index which corresponds to the first address
// effects: may modify p
// time: O(n)
static char *get_available(struct pool *p, int size, int *start) {
  assert(p);
  assert(size > 0);  
  struct llnode *node = p->avail->front;
  bool found = false;
  int diff = 0;          // track how much space after allocating
  int placeholder1 = 0;
  int placeholder2 = 0;
  while (node) {
    if (node->size >= size) {
      found = true;
      diff = node->size - size;
      break;
    }
    node = node->next;
  }
  // node is at the "avail" node (addr, size, start, end)
  if (found) {
    *start = node->start;
    int end = *start + size;
    char *temp = node->addr;
    if (diff == 0) {            // remove the whole node
      remove_node(p->avail, node->addr, &placeholder1, &placeholder2);
    } else {                    // alter the node
      node->addr = &p->data[end];
      node->size = diff;
      node->start += size;
    }
    return temp;
  } else {
    *start = NONE;
    return NULL; 
  }
}

// find_node(p, addr) returns the block of allocated memory that 
//   corresponds to addr, stored in p->used
// requires: addr not NULL
// time: O(n)
static struct llnode *find_node(struct pool *p, char *addr) {
  assert(addr);
  struct llnode *node = p->used->front;
  while (node) {
    if (node->addr == addr) {      // found the addr
      return node;
    } else {
      node = node->next; 
    }
  }
  return node;
}

// find_if_space(p, start, size, i) returns the node which contains the 
//   information provided and updates i to be the size of the desired node 
// effects: may modify i
// time: O(n)
static struct llnode *find_if_space(struct pool *p, int start, 
                                    int size, int *i) {
  struct llnode *node = p->avail->front;
  while (node) {
    // found a node with same starting point and greater or equal size
    if (node->start == start && node->size >= size) {
      *i = node->size - size;
      return node;
    }
    node = node->next;
  }
  return node;
}

// merge_nodes(lst) finds continguous blocks/node and merges them into one  
//   block/node
// effects: may modify node
// time: O(n)
static void merge_nodes(struct llist *lst) {
  assert(lst);
  struct llnode *node = lst->front;
  while (node && node->next) {
    if (node->end >= node->next->start) {
      node->end = node->next->end;
      node->size = node->next->end - node->start;
      struct llnode *holder = node->next;
      node->next = node->next->next;
      lst->len--;
      free(holder);
    } else {
      node = node->next;  
    }
  }
}

char *pool_alloc(struct pool *p, int size) {
  assert(p);
  assert(size > 0);
  int start = 0;
  // get address and remove from p->avail
  char *place = get_available(p, size, &start);
  // if there is space in data
  if (place) {
    p->active_alloc++;
    // add to used
    add_node(p->used, place, size, start);
    return place;
  } else {
    return NULL; 
  }
}

bool pool_free(struct pool *p, char *addr) {
  assert(p);
  int size = 0;
  int start = 0;
  if (!addr) {
    return false;
  }
  // delete from used
  bool removed = remove_node(p->used, addr, &size, &start);
  if (!removed) {
    return false;
  }
  // add to avail + merge if possible
  add_node(p->avail, addr, size, start);
  if (p->avail->front) {
    merge_nodes(p->avail);
  }
  p->active_alloc--;
  return true;
}

char *pool_realloc(struct pool *p, char *addr, int size) {
  assert(p);
  assert(size > 0);
  int diff = 0;
  int new_diff = 0;
  int placeholder1 = 0;
  int placeholder2 = 0;
  if (!addr) {                        // NULL pointers fails
    return NULL;
  }
  struct llnode *original_node = find_node(p, addr);
  int original_size = original_node->size;
  int start = original_node->start;
  if (original_size == NONE) {        // addr DNE 
    return NULL;
  }
  if (size == original_size) {        // do nothing
    return addr;
  } else if (size < original_size) {  // need to shrink
    // update p->used and p->avail accordingly
    remove_node(p->used, addr, &placeholder1, &placeholder2);
    add_node(p->used, addr, size, start);
    add_node(p->avail, &p->data[start + size], size, start + size);
    merge_nodes(p->avail);
    return addr;
  } else {                            // size > original_size
    diff = size - original_size;      // positive
    int end = start + original_size;
    struct llnode *found = find_if_space(p, end, diff, &new_diff);
    if (found) {                      // enough space to extend
      // change size of node in p->used
      original_node->size = size;
      original_node->end = found->start + size;
      // update corresponding node in avail as well
      if (new_diff == 0) {            // whole avail block used up
        remove_node(p->avail, found->addr, &placeholder1, &placeholder2);
      } else {                        // alter information
        found->size -= diff;
        found->start += diff;
        found->addr = &p->data[found->start];
        found->end = found->start + new_diff;
      }
      return addr;
    } else {                          // not enough space to extend
      char *temp = pool_alloc(p, size);
      if (temp) {
        memcpy(temp, addr, sizeof(char));
        pool_free(p, addr);
        return temp;
      } else {
        free(temp);
        return NULL; 
      }
    }
  }
}

void pool_print_active(const struct pool *p) {
  assert(p);
  printf("active: ");
  if (p->active_alloc == 0) {      // no allocations
    printf("none\n");
    return;
  }
  struct llnode *node = p->used->front;
  while (node) {
    printf("%d [%d]", node->start, node->size);
    node = node->next;
    if (node) {
      printf(", ");
    }
  }
  printf("\n");
}

void pool_print_available(const struct pool *p) {
  assert(p);
  printf("available: ");
  struct llnode *node = p->avail->front;
  if (p->avail->len == 0) {    // no available blocks
    printf("none\n");
    return;
  }  
  while (node) {
    printf("%d [%d]", node->start, node->size);
    node = node->next;
    if (node) {
      printf(", ");
    }
  }
  printf("\n");
}
