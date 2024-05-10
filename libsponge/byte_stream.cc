#include "byte_stream.hh"

#include <algorithm>
#include <iterator>
#include <stdexcept>

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity): _capacity(capacity),
_buffer(""), _nread(0), _nwrite(0), _isInputEnded(false) {}

size_t ByteStream::write(const string &data) {
    // decide writed_size
    size_t writed_size = data.size() < remaining_capacity()? data.size(): remaining_capacity();

    // append to the buffer
    _buffer += data.substr(0, writed_size);
    // update the attribute
    _nwrite += writed_size;
    return writed_size;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    return _buffer.substr(0, len);
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    _buffer.erase(0, len);
    _nread += len;
}

void ByteStream::end_input() {
    _isInputEnded = true;
}

bool ByteStream::input_ended() const { return _isInputEnded; }

size_t ByteStream::buffer_size() const { return (_nwrite - _nread); }

bool ByteStream::buffer_empty() const { return (_nwrite == _nread); }

bool ByteStream::eof() const { return _isInputEnded && (_nread == _nwrite); }

size_t ByteStream::bytes_written() const { return _nwrite; }

size_t ByteStream::bytes_read() const { return _nread; }

size_t ByteStream::remaining_capacity() const { return (_capacity - _nwrite + _nread); }
