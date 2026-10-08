#pragma once
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace book {
    class MappedFile {
    private:
        const std::uint8_t *data_ = nullptr;
        std::size_t size_ = 0;
    public:
        explicit MappedFile(const char* path) {
            int fd = ::open(path, O_RDONLY);
            if(fd < 0)
                throw std::runtime_error("cannot open file");
            struct stat st;
            if(::fstat(fd, &st) != 0) {
                ::close(fd);
                throw std::runtime_error("fstat failed");
            }
            size_ = static_cast<std::size_t> (st.st_size);
            void *p = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
            ::close(fd);
            if(p == MAP_FAILED) 
                throw std::runtime_error("mmap failed");
            data_ = static_cast<const std::uint8_t*> (p);
        }

        ~MappedFile() {
            if(data_)
                ::munmap(const_cast<std::uint8_t*>(data_), size_);
        }
        MappedFile(const MappedFile&) = delete;
        MappedFile operator=(const MappedFile&) = delete;

        const std::uint8_t* data() const { return data_; }
        std::size_t size() const { return size_; }
    };
}