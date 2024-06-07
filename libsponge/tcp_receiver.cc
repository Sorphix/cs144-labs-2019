#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

bool TCPReceiver::segment_received(const TCPSegment &seg) {
    // get 'abs_seqno' and 'length' of seg
    size_t length = seg.length_in_sequence_space();
    uint64_t abs_seqno = unwrap(seg.header().seqno, _isn, _abs_checkpoint);

    // SYN seg
    if(seg.header().syn) {
        // refuse redundant SYN
        if(_has_syn)  
            return false;
        // first receive SYN seg
        _has_syn = true;
        _isn = WrappingInt32(seg.header().seqno.raw_value());
        _abs_checkpoint = 1; /*SYN occupy a seqno*/ 

        length --;
        abs_seqno = 1;
        if(length == 0)
            return true;
        // don't have received SYN
    }else if(!_has_syn) { 
        return false;
    }

    // FIN seg
    if(seg.header().fin) {
        if(_has_fin)  // refuse redundant FIN
            return false;
        _has_fin = true;
    }

    // check seg's border to jump over 'push_substring'
    // empty seg
    if(length == 0) {
        if(abs_seqno == _abs_checkpoint)
            return true;
        else
            return false;
    }
    // invalid seg
    if((abs_seqno >= _abs_checkpoint + window_size() || abs_seqno + length - 1 < _abs_checkpoint)) {
        return false;
    }

    _reassembler.push_substring(seg.payload().copy(), abs_seqno - 1, _has_fin);
    /* abs_seq = stream_index + 1 */
    _abs_checkpoint = _reassembler.head_index() + 1;

    // FIN occupy a seqno
    if(_reassembler.stream_out().input_ended())
        _abs_checkpoint++;

    return true;
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if(_abs_checkpoint == 0) 
        return std::nullopt;
    else
        return wrap(_abs_checkpoint, _isn);
}

size_t TCPReceiver::window_size() const {
    return _capacity - _reassembler.stream_out().buffer_size();
}
