#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

void TCPConnection::unclean_close(bool send_rst) {
    // set bytestream to error
    inbound_stream().set_error();
    outbound_stream().set_error();
    _active = false;

    if(send_rst) {
        if(_segments_out.empty())
            _sender.send_empty_segment();
        TCPSegment& head_seg = _sender.segments_out().front();
        head_seg.header().rst = true;
        senders_segments_out();
    }
}

void TCPConnection::senders_segments_out() {

    while(!_sender.segments_out().empty()) {
        TCPSegment seg = _sender.segments_out().front();
        if(_receiver.ackno().has_value()) {
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
            seg.header().win = _receiver.window_size() > UINT16_MAX? UINT16_MAX: _receiver.window_size();
        }
        _sender.segments_out().pop();
        _segments_out.push(seg);
        if(seg.header().rst)
            break;
    }
}

void TCPConnection::segment_received(const TCPSegment &seg) {
    // CLOSE state
    if(!_active) return;

    // if RST flag is set, shutdown the connection
    if(seg.header().rst) {
        unclean_close(false);
        return;
    }

    _time_since_last_segment_received = 0;

    // LISTEN state
    if(!_receiver.ackno().has_value() && _sender.next_seqno_absolute() == 0) {
        // refuse any seg except SYN
        if(!seg.header().syn) return;
        else _ready_to_send = true;
    }

    // SYN_SENT state
    if(!_receiver.ackno().has_value() && _sender.next_seqno_absolute() > 0
    && _sender.next_seqno_absolute() == _sender.bytes_in_flight()) {
        // refuse any seg except SYN+ACK or SYN
        if(!seg.header().syn) return;
    }

    // CLOSE_WAIT or LAST_ACK state
    if(!_linger_after_streams_finish) {
        // refuse data exchange
        if(seg.payload().size() > 0) return;
    }

    // ESTABLISHED state
    if(_receiver.ackno().has_value() && !_receiver.stream_out().input_ended()
    && ((_sender.next_seqno_absolute() > _sender.bytes_in_flight() && !_sender.stream_in().eof())
    || (_sender.stream_in().eof() && _sender.next_seqno_absolute() < _sender.stream_in().bytes_written() + 2))) {
        // if receive FIN passively, set _linger_after_streams_finish to false
        if(seg.header().fin) {
            _linger_after_streams_finish = false;
        }
    }

    // TIME_WAIT state
    bool resend_fin = false;
    if(_receiver.stream_out().input_ended() && _sender.stream_in().eof()
    && _sender.next_seqno_absolute() == _sender.stream_in().bytes_written() + 2
    && _sender.bytes_in_flight() == 0) {
        if(_linger_after_streams_finish && seg.header().fin) {
            // the ACK to FIN has lost, resend it
            resend_fin = true;
        }
    }

    bool ack_for_keep_alive = false;
    // if ACK flag is set, inform the Sender of new ackno and window_size
    if(_sender.next_seqno_absolute() > 0 && seg.header().ack) {
        if(!_sender.ack_received(seg.header().ackno, seg.header().win)) {
            ack_for_keep_alive = true;
        }
    }

    // LAST_ACK state --> CLOSED
    if(!_linger_after_streams_finish && _sender.stream_in().eof()
    && _sender.next_seqno_absolute() == _sender.stream_in().bytes_written() + 2
    && _sender.bytes_in_flight() == 0) {
        _active = false;
    }

    _sender.fill_window();
    // pass the seg to Receiver
    if(!_receiver.segment_received(seg)) {
        ack_for_keep_alive = true;
    }

    // send empty ACK seg
    // 空的正确的ACK不需要被ACK
    if(resend_fin || ack_for_keep_alive || seg.length_in_sequence_space() > 0) {
        if(_sender.segments_out().empty()) {
            _sender.send_empty_segment();
        }
    }

    // push sender's segments out
    senders_segments_out();
}

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) {
    if(outbound_stream().input_ended()) {
        return 0;
    }
    size_t write_size = outbound_stream().write(data);

    _sender.fill_window();
    senders_segments_out();
    return write_size;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    if(!_active || !_ready_to_send) return;

    _time_since_last_segment_received += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);

    // TIME_WAIT state
    if(_receiver.stream_out().input_ended() && _sender.stream_in().eof()
    && _sender.next_seqno_absolute() == _sender.stream_in().bytes_written() + 2
    && _sender.bytes_in_flight() == 0) {
        // clean close
        if(time_since_last_segment_received() >= 10 * _cfg.rt_timeout) {
            _active = false;
            _linger_after_streams_finish = false;
            return;
        }
    }

    // time out, unclean shutdown
    if(_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        // should send RST
        unclean_close(true);
        return;
    }

    _sender.fill_window();
    senders_segments_out();
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _ready_to_send = true;
    // send FIN, turn into FIN_WAIT1 state 
    _sender.fill_window();
    senders_segments_out();
}

void TCPConnection::connect() {
    // send SYN seg
    _ready_to_send = true;
    _sender.fill_window();
    senders_segments_out();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            unclean_close(true);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
