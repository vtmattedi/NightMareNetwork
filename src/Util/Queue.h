#pragma once
#if true
template <typename T>
class Queue<T>
{
private:
    T *m_data;
    size_t m_capacity;
    size_t m_size;
    size_t m_head;
    size_t m_tail;
public:
    Queue(size_t capacity) : m_capacity(capacity), m_size(0), m_head(0), m_tail(0)
    {
        m_data = new T[capacity];
    }

    ~Queue()
    {
        delete[] m_data;
    }

    bool enqueue(const T &item)
    {
        if (m_size == m_capacity)
            return false; // Queue is full
        m_data[m_tail] = item;
        m_tail = (m_tail + 1) % m_capacity;
        ++m_size;
        return true;
    }

    bool dequeue(T &item)
    {
        if (m_size == 0)
            return false; // Queue is empty
        item = m_data[m_head];
        m_head = (m_head + 1) % m_capacity;
        --m_size;
        return true;
    }
    // takes and item from the front queue and puts it at the back of the queue. never full because we took frist item
    bool requeue()
    {
        if (m_size == 0)
            return false; // Queue is empty
        if (m_size == m_capacity)
        {
            m_head = (m_head + 1) % m_capacity;
            m_tail = (m_tail + 1) % m_capacity;
        }
        else
        {
            size_t index = m_head;
            m_head = (m_head + 1) % m_capacity;
            m_tail = (m_tail + 1) % m_capacity;
            m_data[m_tail] = m_data[index];
        }

        return true;
    }

    size_t size() const
    {
        return m_size;
    }
}

#endif // NM_ENABLE_OTA