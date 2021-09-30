# mpsc_zib

Unbounded Multi-Producer Single-Consumer queues for c++

## Repository 

Queues are in self contained headers. There is a bit of duplicate code accross the headers so there is just a single file dependancy for each queue. 

## Using mpsc_zib

Requires gcc 11+

### Tests

Tests can be built:
```bash
mkdir build && cd build
cmake -GNinja -DCMAKE_BUILD_TYPE=Release ..
ninja
ctest
```

## spin_mpsc_queue

The spin queue will never block but can return a nullopt if queue is empty. All operations are lock free and wait free. The queue should be linerizable but needs a more formal proof. 


## wait_mpsc_queue

The wait queue will block when the queue is empty, and resume when the next element is enqueued. This requires an addtional atomic variable and a couple of extra atomic ops. All operations (except on empty dequeue) are lock free and wait free. The queue should be linerizable but needs a more formal proof. 




