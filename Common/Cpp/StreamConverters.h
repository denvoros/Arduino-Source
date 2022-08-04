/*  Stream Converters
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 */

#ifndef PokemonAutomation_StreamConverters_H
#define PokemonAutomation_StreamConverters_H

#include <set>
#include "AlignedVector.h"

namespace PokemonAutomation{


class StreamListener{
public:
    StreamListener(size_t p_object_size)
        : object_size(p_object_size)
    {}
    virtual ~StreamListener() = default;
    virtual void on_objects(const void* data, size_t objects) = 0;

    const size_t object_size;
};



class StreamConverter{
public:
    void add_listener(StreamListener& listener);
    void remove_listener(StreamListener& listener);

public:
    StreamConverter(
        size_t object_size_in,
        size_t object_size_out,
        size_t buffer_capacity
    );
    virtual ~StreamConverter();

    void push_objects(const void* data, size_t objects);

protected:
    virtual void convert(void* out, const void* in, size_t count) = 0;

private:
    size_t m_object_size_in;
    size_t m_object_size_out;

    size_t m_buffer_capacity;
    AlignedVector<char> m_buffer;

    std::set<StreamListener*> m_listeners;
};



class MisalignedStreamConverter{
public:
    void add_listener(StreamListener& listener);
    void remove_listener(StreamListener& listener);

public:
    MisalignedStreamConverter(
        size_t object_size_in,
        size_t object_size_out,
        size_t buffer_capacity
    );
    virtual ~MisalignedStreamConverter();

    void push_bytes(const void* data, size_t bytes);

protected:
    virtual void convert(void* out, const void* in, size_t count) = 0;

private:
    size_t m_object_size_in;
    size_t m_object_size_out;

    AlignedVector<char> m_edge;
    size_t m_edge_size = 0;

    size_t m_buffer_capacity;
    AlignedVector<char> m_buffer;

    std::set<StreamListener*> m_listeners;
};




}
#endif
