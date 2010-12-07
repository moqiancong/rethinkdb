#include "static_header.hpp"
#include "config/args.hpp"
#include "utils.hpp"
#include "coroutine/coroutines.hpp"

void static_header_check(direct_file_t *file, static_header_check_callback_t *cb) {
    
    if (file->get_size() < DEVICE_BLOCK_SIZE) {
        cb->on_static_header_check(false);
    } else {
        static_header_t *buffer = (static_header_t *)malloc_aligned(DEVICE_BLOCK_SIZE, DEVICE_BLOCK_SIZE);
        file->read_blocking(0, DEVICE_BLOCK_SIZE, buffer);
        if (memcmp(buffer, SOFTWARE_NAME_STRING, sizeof(SOFTWARE_NAME_STRING)) == 0) {
            cb->on_static_header_check(true);
        } else {
            cb->on_static_header_check(false);
        }
        free(buffer);
    }
}

struct static_header_write_fsm_t :
    public iocallback_t
{
    static_header_write_callback_t *callback;
    static_header_t *buffer;
    static_header_write_fsm_t() {
        buffer = (static_header_t *)malloc_aligned(DEVICE_BLOCK_SIZE, DEVICE_BLOCK_SIZE);
    }
    ~static_header_write_fsm_t() {
        free(buffer);
    }
    void on_io_complete(event_t *e) {
        if (callback) callback->on_static_header_write();
        delete this;
    }
};

bool static_header_write(direct_file_t *file, void *data, size_t data_size, static_header_write_callback_t *cb) {
    
    static_header_write_fsm_t *fsm = new static_header_write_fsm_t();
    fsm->callback = cb;
    
    assert(sizeof(static_header_t) + data_size < DEVICE_BLOCK_SIZE);
        
    file->set_size_at_least(DEVICE_BLOCK_SIZE);
    
    bzero(fsm->buffer, DEVICE_BLOCK_SIZE);
    
    assert(sizeof(SOFTWARE_NAME_STRING) < 16);
    memcpy(fsm->buffer->software_name, SOFTWARE_NAME_STRING, sizeof(SOFTWARE_NAME_STRING));
    
    assert(sizeof(VERSION_STRING) < 16);
    memcpy(fsm->buffer->version, VERSION_STRING, sizeof(VERSION_STRING));
    
    memcpy(fsm->buffer->data, data, data_size);
    
    bool done __attribute__((unused)) = file->write_async(0, DEVICE_BLOCK_SIZE, fsm->buffer, fsm);
    assert(!done);
    
    return false;
}

struct file_read_adapter_t : public iocallback_t {
    coro_t *cont;
    file_read_adapter_t() : cont(coro_t::self()) { }
    void on_io_complete(event_t *e) {
        printf("I completed!\n");
        cont->notify();
        delete this;
    }
};

void read_file_blocking(direct_file_t *file, size_t offset, size_t length, void *buf) {
    printf("I'll block\n");
    file->read_async(offset, length, buf, new file_read_adapter_t());
    coro_t::wait();
    printf("And now the blocking is done.\n");
}

void run_static_header_read(direct_file_t *file, static_header_read_callback_t *callback, static_header_t *buffer, void *data_out, size_t data_size) {
    printf("I'm using coroutines!\n");
    read_file_blocking(file, 0, DEVICE_BLOCK_SIZE, buffer);
    if (memcmp(buffer->software_name, SOFTWARE_NAME_STRING, sizeof(SOFTWARE_NAME_STRING)) != 0) {
        fail("This doesn't appear to be a RethinkDB data file.");
    }
    if (memcmp(buffer->version, VERSION_STRING, sizeof(VERSION_STRING)) != 0) {
        fail("File version is incorrect. This file was created with version %s of RethinkDB, "
            "but you are trying to read it with version %s.", buffer->version, VERSION_STRING);
    }
    memcpy(data_out, buffer->data, data_size);
    callback->on_static_header_read();
    free(buffer);
}

bool static_header_read(direct_file_t *file, void *data_out, size_t data_size, static_header_read_callback_t *cb) {
    assert(sizeof(static_header_t) + data_size < DEVICE_BLOCK_SIZE);
    
    static_header_t *buffer = (static_header_t*)malloc_aligned(DEVICE_BLOCK_SIZE, DEVICE_BLOCK_SIZE);
    new auto_coro_t(run_static_header_read, file, cb, buffer, data_out, data_size);
    
    return false;
}
