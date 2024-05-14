#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity) {}

// Merge db2 into db1
// return merged byte, -1 if cannot merge
long StreamReassembler::merge_blocks(DataBlock &db1, const DataBlock &db2) {
    DataBlock g, t;

    // decide tiny or giant
    if(db1.head_index < db2.head_index) {
        g = db1;
        t = db2;
    }else {
        g = db2;
        t = db1;
    }

    // merge tiny into giant
    if(t.head_index + t.length <= g.head_index + g.length) {
        // tiny is inside giant
        db1 = g;
        return t.length;
    }else if(g.head_index + g.length < t.head_index) {
        // there is no intersection
        return -1;
    }else {
        // tiny's end is beyond giant's
        int offset = t.head_index + t.length - g.head_index - g.length;
        g.data += t.data.substr(g.head_index + g.length - t.head_index, offset);
        g.length += offset; 
        db1 = g;
        return t.length - offset;
    }
}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    DataBlock db, db0;
    long merged_bytes;

    if(eof) _eof_flag = true;

    // Check if the index is valid
    // And construct the DataBlock for RB-tree
    if(index > _head_index + _capacity || index + data.size() <= _head_index) {
        // the segment is already assembled or beyond capacity
        // "<=" indicate you cannot get a empty block when you cut off
        goto JUDGE_EOF;
    }else if(index < _head_index) {
        // cut off the edge when needed
        int offset = _head_index - index;
        db.head_index = _head_index;
        db.data.assign(data.begin() + offset, data.end());
        db.length = data.size() - offset;
    }else {
        db.head_index = index;
        db.data.assign(data.begin(), data.end());
        db.length = data.size();
    }

    // update the attribute 
    _unassembled += db.length;
    // Merge the nearby DataBlock into db
    // look back
    while(1) {
        auto iter = _blocks.lower_bound(db);
        if(iter == _blocks.end()) {
            break;
        }
        if((merged_bytes = merge_blocks(db, *iter)) >= 0) {
            _unassembled -= merged_bytes;
            _blocks.erase(iter);
        }else {
            break;
        }
    }
    // look ahead
    while(1) {
        auto iter = _blocks.lower_bound(db);
        if(iter == _blocks.begin()) {
            break;
        }
        iter--;
        if((merged_bytes = merge_blocks(db, *iter)) >= 0) {
            _unassembled -= merged_bytes;
            _blocks.erase(iter);
        }else {
            break;
        } 
    }
    // update the _blocks afterward
    _blocks.insert(db);

    // check if the first block need to be pushed to ByteStream
    db0 = *_blocks.begin();
    if(db0.head_index == _head_index) {
        _output.write(db0.data);
        // update the attribute
        _unassembled -= db0.length;
        _head_index += db0.length;
        _blocks.erase(db0);
    }

JUDGE_EOF:
    if(_eof_flag && empty()) {
        _output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembled; }

bool StreamReassembler::empty() const { return _unassembled == 0; }
