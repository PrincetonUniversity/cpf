#include <boost/lockfree/spsc_queue.hpp> // ring buffer

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/string.hpp>

namespace bip = boost::interprocess;
namespace shm
{
    typedef bip::allocator<char, bip::managed_shared_memory::segment_manager> char_alloc;
    typedef bip::basic_string<char, std::char_traits<char>, char_alloc >      shared_string;

    // shared_string, 
    using ring_buffer = boost::lockfree::spsc_queue<unsigned int, boost::lockfree::capacity<65536>>;
}

#include <iostream>

int main()
{
    // create segment and corresponding allocator
    bip::managed_shared_memory segment(bip::open_or_create, "MySharedMemory", 104857600);
    shm::char_alloc char_alloc(segment.get_segment_manager());

    shm::ring_buffer *queue = segment.find_or_construct<shm::ring_buffer>("queue")();

    auto counter = 0;
    // shm::shared_string v(char_alloc);
    unsigned int v;
    while (true)
    {
        if (queue->pop(v)) {
          counter++;

          if (counter % 1000000 == 0) {
            std::cout << "counter: " << counter << std::endl;
          }
        }
    }
}
