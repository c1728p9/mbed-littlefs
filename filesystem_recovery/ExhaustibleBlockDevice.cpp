/* mbed Microcontroller Library
 * Copyright (c) 2017 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ExhaustibleBlockDevice.h"


static void program_data(uint8_t *dst, const uint8_t *src, bd_size_t size, bool prog_exhausted, bool erase_exhausted)
{
    if (prog_exhausted || erase_exhausted) {
        return;
    }
    memcpy(dst, src, size);
}

static void erase_data(uint8_t *dst, bd_size_t size, bool exhausted)
{
    if (exhausted) {
        return;
    }
    memset(dst, 0xFF, size);
}

ExhaustibleBlockDevice::ExhaustibleBlockDevice(bd_size_t size, bd_size_t read, bd_size_t program, bd_size_t erase)
    : _blocks(0), _read_size(read)
    , _program_size(program), _program_count(size / program), _program_array(new uint32_t[_program_count]()), _program_life(0)
    , _erase_size(erase), _erase_count(size / erase), _erase_array(new uint32_t[_erase_count]()), _erase_life(0)
{
    MBED_ASSERT(_program_count * _program_size == size);
    MBED_ASSERT(_erase_count * _erase_size == size);
}

ExhaustibleBlockDevice::~ExhaustibleBlockDevice()
{
    if (_blocks) {
        for (size_t i = 0; i < _erase_count; i++) {
            delete[] _blocks[i];
            _blocks[i] = 0;
        }

        delete[] _program_array;
        delete[] _erase_array;
        delete[] _blocks;
        _program_array = 0;
        _erase_array = 0;
        _blocks = 0;
    }
}

void ExhaustibleBlockDevice::set_program_cycles(uint32_t cycles)
{
    MBED_ASSERT(NULL == _blocks);
    _program_life = cycles;
}

void ExhaustibleBlockDevice::set_erase_cycles(uint32_t cycles)
{
    MBED_ASSERT(NULL == _blocks);
    _erase_life = cycles;
}

int ExhaustibleBlockDevice::init()
{
    if (!_blocks) {
        _blocks = new uint8_t*[_erase_count];
        for (size_t i = 0; i < _erase_count; i++) {
            _blocks[i] = 0;
        }
    }

    return BD_ERROR_OK;
}

int ExhaustibleBlockDevice::deinit()
{
    MBED_ASSERT(_blocks != NULL);
    // Memory is lazily cleaned up in destructor to allow
    // data to live across de/reinitialization
    return BD_ERROR_OK;
}

bd_size_t ExhaustibleBlockDevice::get_read_size() const
{
    MBED_ASSERT(_blocks != NULL);
    return _read_size;
}

bd_size_t ExhaustibleBlockDevice::get_program_size() const
{
    MBED_ASSERT(_blocks != NULL);
    return _program_size;
}

bd_size_t ExhaustibleBlockDevice::get_erase_size() const
{
    MBED_ASSERT(_blocks != NULL);
    return _erase_size;
}

bd_size_t ExhaustibleBlockDevice::size() const
{
    MBED_ASSERT(_blocks != NULL);
    return _erase_count * _erase_size;
}

int ExhaustibleBlockDevice::read(void *b, bd_addr_t addr, bd_size_t size)
{
    MBED_ASSERT(_blocks != NULL);
    MBED_ASSERT(is_valid_read(addr, size));
    uint8_t *buffer = static_cast<uint8_t*>(b);

    while (size > 0) {
        bd_addr_t hi = addr / _erase_size;
        bd_addr_t lo = addr % _erase_size;

        if (_blocks[hi]) {
            memcpy(buffer, &_blocks[hi][lo], _read_size);
        } else {
            memset(buffer, 0, _read_size);
        }

        buffer += _read_size;
        addr += _read_size;
        size -= _read_size;
    }

    return 0;
}

int ExhaustibleBlockDevice::program(const void *b, bd_addr_t addr, bd_size_t size)
{
    MBED_ASSERT(_blocks != NULL);
    MBED_ASSERT(is_valid_program(addr, size));
    const uint8_t *buffer = static_cast<const uint8_t*>(b);
    _lazy_allocate(addr, size);

    while (size > 0) {
        bd_addr_t hi = addr / _erase_size;
        bd_addr_t lo = addr % _erase_size;
        uint32_t prog_index = addr /_program_size;
        uint32_t erase_index = addr /_erase_size;

        bool prog_cycles_reached = _program_life && (_program_array[prog_index] > _program_life);
        bool erase_cycles_reached = _erase_life && (_erase_array[erase_index] > _erase_life);
        program_data(&_blocks[hi][lo], buffer, _program_size, prog_cycles_reached, erase_cycles_reached);
        if (!prog_cycles_reached) {
            _program_array[prog_index]++;
        }

        buffer += _program_size;
        addr += _program_size;
        size -= _program_size;
    }

    return 0;
}

int ExhaustibleBlockDevice::erase(bd_addr_t addr, bd_size_t size)
{
    MBED_ASSERT(_blocks != NULL);
    MBED_ASSERT(is_valid_erase(addr, size));
    _lazy_allocate(addr, size);

    while (size > 0) {
        uint32_t erase_index = addr /_erase_size;

        bool erase_cycles_reached = _erase_life && (_erase_array[erase_index] > _erase_life);
        erase_data(_blocks[erase_index], _erase_size, erase_cycles_reached);
        if (!erase_cycles_reached) {
            _erase_array[erase_index]++;
        }

        addr += _erase_size;
        size -= _erase_size;
    }

    return 0;
}

void ExhaustibleBlockDevice::_lazy_allocate(bd_addr_t addr, bd_size_t size)
{
    const uint32_t start_index = addr / _erase_size;
    const uint32_t end_index = (addr + size) / _erase_size;
    for (uint32_t i = start_index; i < end_index; i++) {
        if (!_blocks[i]) {
            _blocks[i] = new uint8_t[_erase_size]();
        }
    }
}


