#include <atomic>
#include <mutex>

using data_t = int;
using mutex_t = std::mutex;
static constexpr int LFQ_CACHE_LINE = 64;

struct node_t {
  node_t(data_t v, node_t *n = nullptr) : value(v), next(n) {}
  data_t value;
  std::atomic<node_t *> next;
};
// Two-lock queue (Michael & Scott). Separate head/tail cache lines.
struct queue_t {
  alignas(LFQ_CACHE_LINE) node_t *head;
  mutex_t h_lock;
  alignas(LFQ_CACHE_LINE) node_t *tail;
  mutex_t t_lock;
};

static inline node_t *new_node(data_t value) { return new node_t(value); }

static inline void free_node(node_t *p) { delete p; }

static inline void initialize(queue_t *q, data_t value) {
  node_t *node = new_node(value); // Allocate a new node
  // Make it the only node in the queue
  q->head = node; // Both head and tail point to it
  q->tail = node;
}

static inline void enqueue(queue_t *q, data_t value) {
  node_t *node = new_node(value); // Allocate a new node
  // Copy enqueued value into node
  // Set next pointer of node to nullptr
  std::lock_guard<std::mutex> lg(q->t_lock); // Acquire t_lock to access tail
  q->tail->next.store(
      node, std::memory_order_release); // Append node at the end of queue

  q->tail = node; // Swing tail to node
}

static inline bool dequeue(queue_t *q, data_t *pvalue) {
  node_t *node = nullptr;
  {
    std::lock_guard<std::mutex> lg(q->h_lock); // Acquire h_lock to access head
    node = q->head;                            // Read head
    node_t *new_head =
        node->next.load(std::memory_order_acquire); // Read next pointer
    if (new_head == nullptr) {                      // Is queue empty?
      return false;                                 // Queue was empty
    }
    *pvalue = new_head->value; // Queue not empty. Read value
    q->head = new_head;        // Swing head to next node
  }
  free_node(node); // Free node
  return true;     // Dequeue succeeded
}
