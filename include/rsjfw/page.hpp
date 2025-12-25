#pragma once

#include <string>
#include <stack>
#include <memory>
#include <functional>

namespace rsjfw {

class Page {
public:
    virtual ~Page() = default;
    virtual void render() = 0;
    virtual std::string title() const = 0;
    virtual bool canGoBack() const { return true; }
};

class PageStack {
public:
    void push(std::shared_ptr<Page> page) {
        pages_.push(page);
    }
    
    void pop() {
        if (pages_.size() > 1) {
            pages_.pop();
        }
    }
    
    void replace(std::shared_ptr<Page> page) {
        if (!pages_.empty()) pages_.pop();
        pages_.push(page);
    }
    
    Page* current() const {
        return pages_.empty() ? nullptr : pages_.top().get();
    }
    
    size_t depth() const { return pages_.size(); }
    bool canGoBack() const { return pages_.size() > 1 && current()->canGoBack(); }

private:
    std::stack<std::shared_ptr<Page>> pages_;
};

}
