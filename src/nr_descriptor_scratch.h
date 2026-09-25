#pragma once
#include <array>
#include <memory>
#include <new>
#include <cstddef>
namespace nr_descriptor {
template<class T,std::size_t Capacity,std::size_t CachedBlocks=2>
class ScratchBuffer {
    struct Block {std::array<T,Capacity> items{};bool busy=false;};
    static auto& Cache(){static thread_local std::array<std::unique_ptr<Block>,CachedBlocks> blocks;return blocks;}
    Block* cached_=nullptr;
    std::unique_ptr<T[]> owned_;
    T* data_=nullptr;
public:
    ScratchBuffer()=default;
    ScratchBuffer(const ScratchBuffer&)=delete;ScratchBuffer& operator=(const ScratchBuffer&)=delete;
    ~ScratchBuffer(){if(cached_)cached_->busy=false;}
    bool Acquire(std::size_t count) noexcept {
        if(data_||!count)return false;
        if(count<=Capacity){
            for(auto& block:Cache()){
                if(!block)block.reset(new(std::nothrow) Block);
                if(block&&!block->busy){block->busy=true;cached_=block.get();data_=block->items.data();return true;}
            }
        }
        owned_.reset(new(std::nothrow) T[count]);data_=owned_.get();return data_!=nullptr;
    }
    T* get() const noexcept{return data_;}
    explicit operator bool() const noexcept{return data_!=nullptr;}
    T& operator[](std::size_t index) noexcept{return data_[index];}
};
}
