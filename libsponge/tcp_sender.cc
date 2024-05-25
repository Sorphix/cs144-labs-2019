#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _retransmission_timeout{retx_timeout}
    , _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const { return _next_seqno - _abs_ackno; }

void TCPSender::fill_window() {
    // FOR SYN SEG
    if(!_syn_flag) {
        _syn_flag = true;
        TCPSegment seg;
        seg.header().syn = true;
        send_segment(seg);
        // don't do anything else
        return;
    }

    // send as many TCPsegs as possible to fulfill the window
    size_t window_size/*发送窗口*/ = _window_size == 0? 1: _window_size;
    size_t window_remain/*可用窗口*/ = _abs_ackno + window_size - _next_seqno;
    while(window_remain > 0 && !_fin_flag) {
        // decide the size of segment ,and assign the data to the segment
        size_t length = window_remain > TCPConfig::MAX_PAYLOAD_SIZE? TCPConfig::MAX_PAYLOAD_SIZE: window_remain;
        TCPSegment seg;
        string str = _stream.read(length);
        seg.payload() = Buffer(std::move(str));

        // get the size readed
        size_t real_length = seg.length_in_sequence_space();
        // FOR FIN SEG
        // stream.read has reach the bottom
        if(real_length < length && _stream.input_ended()) {
            seg.header().fin = true;
            _fin_flag = true;
        }
        if(seg.length_in_sequence_space() == 0) {
            return; // stream is empty
        }

        send_segment(seg);
        window_remain -= seg.length_in_sequence_space();
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
//! \returns `false` if the ackno appears invalid (acknowledges something the TCPSender hasn't sent yet)
bool TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t abs_ackno = unwrap(ackno, _isn, _abs_ackno);
    // invalid ackno, beyond the sended seqno
    if(abs_ackno > _next_seqno) {
        return false;
    }
    // ack has been received
    if(abs_ackno < _abs_ackno) {
        return true;
    }

    _abs_ackno = abs_ackno;
    _window_size = size_t(window_size);
    // pop segments before ackno in '_segments_not_ack'
    while(!_segments_not_ack.empty()) {
        TCPSegment seg = _segments_not_ack.front();
        if(unwrap(seg.header().seqno, _isn, _abs_ackno) + seg.length_in_sequence_space() <= abs_ackno) {
            _segments_not_ack.pop();
        }else {
            break;
        }
    }

    // fill window after update window size
    fill_window();

    // TIMER restart as a new row is transmitted
    _retransmission_timeout = _initial_retransmission_timeout;
    _consecutive_retransmissions = 0;
    if(!_segments_not_ack.empty()) {
        _timer_start_flag = true;
        _timer = 0;
    }

    return true;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    _timer += ms_since_last_tick;

    if(_timer >= _retransmission_timeout) {
        if(!_segments_not_ack.empty()) {
            // retransmit
            TCPSegment seg = _segments_not_ack.front();
            _segments_out.push(seg);
            // keep track of consecutive_retransmissions and double the RTO
            _consecutive_retransmissions++;
            _retransmission_timeout *= 2;
            // restart the timer
            _timer_start_flag = true;
            _timer = 0;
        }else {
            // no need for timer
            _timer_start_flag = false;
        }
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(seg);
}

void TCPSender::send_segment(TCPSegment& seg) {
    seg.header().seqno = wrap(_next_seqno, _isn);
    // update attribute
    _next_seqno += seg.length_in_sequence_space();
    _segments_not_ack.push(seg);
    _segments_out.push(seg);
    // TIEMR start if it's not started yet
    if(_timer_start_flag == false) {
        _timer_start_flag = true;
        _timer = 0;
    }
}