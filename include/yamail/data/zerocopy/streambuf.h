#ifndef _YAMAIL_DATA_ZEROCOPY_STREAMBUF_H_
#define _YAMAIL_DATA_ZEROCOPY_STREAMBUF_H_

#include <yamail/config.h>
#include <yamail/data/zerocopy/namespace.h>

#include <yamail/data/zerocopy/fragment.h>
#include <yamail/data/zerocopy/segment.h>
#include <yamail/data/zerocopy/iterator.h>

#include <yamail/compat/shared_ptr.h>

#include <list>
#include <vector>
#include <algorithm> // std::min
#include <memory>
#include <streambuf>
#include <cstring> // memcpy

#include <boost/limits.hpp>
#include <boost/throw_exception.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/noncopyable.hpp>
#include <boost/range/iterator_range.hpp>

// #define YDEBUG 1
//
#if defined(YDEBUG)
#include <iostream>

#define PRINT_DEBUG(os) print_debug(os, __LINE__)
#endif

YAMAIL_FQNS_DATA_ZC_BEGIN

namespace asio = ::boost::asio;

template<
// used for streambuf
        typename CharT = char, typename Traits = std::char_traits<CharT>
        // used for allocating IO buffers (fragments)
        , typename FragmentAlloc = std::allocator<CharT>
        // used for allocating everithing else,
        // mostly: internal containers entries, shared_ptr internals
        , typename Alloc = std::allocator<void> >
class basic_streambuf: public std::basic_streambuf<CharT, Traits>,
        boost::noncopyable {
public:
    typedef CharT char_type;
protected:
    typedef Traits traits_type;
    typedef FragmentAlloc fragment_allocator_type;
    typedef Alloc allocator_type;

    typedef std::basic_streambuf<char_type, traits_type> streambuf_type;

    typedef detail::basic_raii_fragment<fragment_allocator_type> fragment_type;
    typedef compat::shared_ptr<fragment_type> fragment_ptr;

    typedef typename fragment_type::const_iterator fragment_const_iterator;

    typedef typename allocator_type::template rebind<fragment_ptr>::other fragment_list_allocator;

    typedef std::list<fragment_ptr, fragment_list_allocator> fragment_list;
    typedef typename fragment_list::const_iterator fragment_list_const_iterator;
private:
    // private vars

    std::size_t max_size_;

    // probably not needed
    std::size_t size_;

    std::size_t inter_size_; // bytes between egptr and pbase
    std::size_t tail_size_;  // bytes after epptr

    // probably not needed,
    // = size () + put_size () + (eback - fragments_.begin)
    std::size_t total_size_;

    std::size_t min_fragmentation_;
    std::size_t max_fragmentation_;

    fragment_allocator_type fallocator_;
    allocator_type allocator_;

    typename allocator_type::template rebind<fragment_type>::other
        fragment_type_allocator_;

    fragment_list fragments_;

    // active 'put' fragment - this is where pbase, pptr, epptr refers to
    fragment_list_const_iterator put_active_; 

    // utility functions

    // returns the occupied space by of fragment pointed by iterator 'iter'
    std::size_t fragment_size(fragment_list_const_iterator const& iter) const {
        return (*iter)->size();
    }

    std::size_t inter_size() const {
        return inter_size_;
    }

    std::size_t tail_size() const {
        return tail_size_;
    }

    /**
     * Continuous put size within current fragment
     */
    std::size_t fragment_put_size() const {
        return std::size_t(this->epptr() - this->pptr());
    }

    std::size_t put_size() const {
        return tail_size() + fragment_put_size();
    }

    std::size_t total_size() const {
        return total_size_;
    }

    // XXX: may be "&" ?
    fragment_list_const_iterator const get_active() const {
        return fragments_.begin();
    }

    fragment_list_const_iterator const& put_active() const {
        return put_active_;
    }

    fragment_list_const_iterator& put_active() {
        return put_active_;
    }

    // check if iterator points to the last fragments_ element
    template<typename FragmentsIterator>
    bool points_to_last_fragment(FragmentsIterator const& iter) const {
        assert(!fragments_.empty());
        return *fragments_.rbegin() == *iter;
    }

    void add_fragment( std::size_t size ) {
        fragments_.push_back(
                boost::allocate_shared<fragment_type>(
                  fragment_type_allocator_, size, fallocator_));
        tail_size_ += size;
    }

#if YDEBUG
    template <typename C, typename T>
    void print_debug (std::basic_ostream<C,T>& os, int line) const
    {
        // print line number
        os << line << ": ";

        // get area is always first one
        os << "[";

        // print unused
        if (this->eback () != (*get_active ())->begin ())
        os << (this->eback () - (*get_active ())->begin ()) << '-';

        // print get area
        os << "(G:" << (this->gptr () - this->eback ()) << ':'
        << (this->egptr () - this->gptr ()) << ')';

        // print area between get and put
        if (get_active () != put_active ())
        {
            if (this->egptr () != (*get_active ())->end ())
            os << '-' << ((*get_active ())->end () - this->egptr ());

            os << "] ";

            // print fragments between get and put
            fragment_list_const_iterator iter = get_active ();
            while (++iter != put_active ())
                os << '[' << (*iter)->size() << "] ";

            os << "[";
            if (this->pbase () != (*put_active ())->begin ())
            os << (this->pbase () - (*put_active ())->begin ()) << '-';
        }
        else
        os << '-';

        // print put area
        os << "(P:" << (this->pptr () - this->pbase ()) << ':'
        << (this->epptr () - this->pptr ()) << ')';

        std::size_t tail = 0;
        // print free
        if (this->epptr () != (*put_active ())->end ())
        {
            os << '-' << ((*put_active ())->end () - this->epptr ());
            tail += ((*put_active ())->end () - this->epptr ());
        }

        os << ']';

        fragment_list_const_iterator iter = put_active ();
        while (++iter != fragments_.end ())
        {
            os << " [" << (*iter)->size() << ']';
            tail += (*iter)->size();
        }

        os << '\n';

        os << "TAIL=" << tail << ", TAIL_SIZE=" << tail_size () << "\n";
        assert (tail == tail_size ());
    }
#endif
public:

    typedef typename allocator_type::template rebind<asio::mutable_buffer>::
      other mutable_buffers_allocator;

    typedef std::vector<asio::mutable_buffer, mutable_buffers_allocator> 
      mutable_buffers_type;

    typedef typename allocator_type::template rebind<asio::const_buffer>::
      other const_buffers_allocator;

    typedef std::vector<asio::const_buffer, const_buffers_allocator> 
      const_buffers_type;

    template<typename T>
    struct iteratorT {
        typedef zerocopy::iterator<T, fragment_type, fragment_list> type;
    };

    typedef typename iteratorT<char_type>::type iterator;

    typedef basic_segment<allocator_type> segment_type;

    fragment_allocator_type const& get_fallocator() const {
        return fallocator_;
    }

    allocator_type const& get_allocator() const {
        return allocator_;
    }

    static const std::size_t default_max_size;
    static const std::size_t default_max_fragmentation;
    static const std::size_t default_min_fragmentation;

    std::size_t constarin_max_fragmentation( std::size_t v ) const {
        if (!v) {
            v = default_max_fragmentation;
        }
        return std::max(min_fragmentation_, v);
    }

    std::size_t constarin_min_fragmentation( std::size_t v ) const {
        return v ? v : default_min_fragmentation;
    }

    void init (std::size_t start_fragments
        , std::size_t start_fragment_len, std::size_t max_size)
    {
        if (!start_fragment_len) {
            start_fragment_len = min_fragmentation_;
        }

        if (!start_fragments) {
            start_fragments = 1;
        }

        if (!max_size) {
            max_size = default_max_size;
        }

        max_size_ = max_size;

        while (start_fragments--) {
            add_fragment(start_fragment_len);
        }

        total_size_ = put_size();

        assert(!fragments_.empty());

        // reset 'get' iterators to the fragments begin
        put_active_ = fragments_.begin();

        // set streambuf pointers
        const typename fragment_type::iterator get_begin = 
            (*get_active())->begin();
        std::streambuf::setg(get_begin, get_begin, get_begin);

        update_put();
    }

    basic_streambuf(std::size_t min_fragmentation = default_min_fragmentation,
            std::size_t max_fragmentation = default_max_fragmentation,
            std::size_t start_fragments = 0,    // defaults to 1
            std::size_t start_fragment_len = 0, // defaults to fragmentation
            std::size_t max_size = default_max_size,
            fragment_allocator_type const& fallocator = fragment_allocator_type(),
            allocator_type const& allocator = allocator_type())
    : size_(0), inter_size_(0), tail_size_(0), total_size_(0),
            min_fragmentation_( constarin_min_fragmentation(min_fragmentation) ),
            max_fragmentation_( constarin_max_fragmentation(max_fragmentation) ),
            fallocator_(fallocator), allocator_(allocator), 
            fragment_type_allocator_ (allocator_), fragments_(allocator_)
    {
    	init (start_fragments, start_fragment_len, max_size);
    }

    // Compatible with default constructor of std::basic_streambuf
    basic_streambuf (std::size_t max_size,
        fragment_allocator_type const& fallocator = fragment_allocator_type(),
        allocator_type const& allocator = allocator_type())
    : size_(0), inter_size_(0), tail_size_(0), total_size_(0),
            min_fragmentation_( constarin_min_fragmentation(min_fragmentation) ),
            max_fragmentation_( constarin_max_fragmentation(max_fragmentation) ),
            fallocator_(fallocator), allocator_(allocator), 
            fragment_type_allocator_ (allocator_), fragments_(allocator_)
    {
    	init (0, 0, max_size);
    }

    iterator begin() const {
        return iterator(fragments_, this->gptr(), get_active());
    }

    iterator end() const {
        return iterator(fragments_, this->pptr(), put_active());
    }

    segment_type detach(iterator const& split_point) {
#if defined(YDEBUG)
        std::cerr << "DETACH ()\n";
        PRINT_DEBUG (std::cerr);
#endif

        fragment_list_const_iterator iter = get_active();

        while (iter != split_point.fragment_) {
            assert(iter != fragments_.end());
            ++iter;
        }

        segment_type tmp(boost::make_iterator_range(get_active(), ++iter),
                this->gptr(), split_point.pos_);

        // consume first fragment
        if (get_active() == put_active()) {
            assert(split_point.fragment_ == get_active());
            assert(split_point.pos_ >= this->gptr());
            assert(split_point.pos_ <= this->pptr());

            if (split_point.pos_ > this->pbase()) {
                compact_put_area();
            }

            inter_size_ = 0;
            std::streambuf::setg(const_cast<char_type*>(split_point.pos_),
                    const_cast<char_type*>(split_point.pos_),
                    this->pbase());

            return tmp;
        }

        if (split_point.fragment_ == get_active()
                && (*get_active())->contains(split_point.pos_)) {
#if defined (YDEBUG)
            std::cerr << "DETACH_1: intersize: " <<
            inter_size_ << " -= " <<
            ((*get_active ())->end () - this->egptr ()) << "\n";
#endif

            assert(split_point.pos_ >= this->gptr());

            inter_size_ -= ((*get_active())->end() - this->egptr());

            // extend get ptrs to the end of fragment
            std::streambuf::setg(const_cast<char_type*>(split_point.pos_),
                    const_cast<char_type*>(split_point.pos_),
                    (*get_active())->end());
#if defined(YDEBUG)
            size ();
            PRINT_DEBUG (std::cerr);
#endif
            return tmp;
        }

        // drop get_active node
        inter_size_ -= (*get_active())->end() - this->egptr();
        fragments_.pop_front();

        // remove everithing till cur_seg
        while (get_active() != split_point.fragment_) {
            assert(get_active() != put_active());
            inter_size_ -= (*get_active())->size();
            fragments_.pop_front();
        }

        if (get_active() == put_active()) {
            assert(split_point.pos_ <= this->pptr());

            if (split_point.pos_ > this->pbase()) {
                compact_put_area();
            }

            inter_size_ = 0;
            std::streambuf::setg(const_cast<char_type*>(split_point.pos_),
                    const_cast<char_type*>(split_point.pos_),
                    this->pbase());

        } else {
#if defined (YDEBUG)
            std::cerr << "DETACH_1: intersize: " <<
            inter_size_ << " -= " <<
            (*get_active())->size() << "\n";
#endif
            assert(split_point.pos_ >= (*get_active())->begin());
            assert(split_point.pos_ <= (*get_active())->end());
            assert(inter_size_ >= (*get_active())->size());

            inter_size_ -= (*get_active())->size();

            // extend get ptrs to the end of fragment
            std::streambuf::setg(split_point.pos_, split_point.pos_,
                    (*get_active())->end());
        }
#if defined(YDEBUG)
        this->size ();
        std::cerr << "DETACH - OK, tailsize=" << tail_size ()
        << ", putsize=" << put_size () << "\n";
        PRINT_DEBUG (std::cerr);
#endif

        return tmp;
    }

    // get the size of the input area
    std::size_t size() const {
#if defined(YDEBUG)
        std::cerr << "SIZE () = "
        << inter_size () << " + " <<
        (this->egptr () - this->gptr ()) << " + " <<
        (this->pptr () - this->pbase ()) << "\n";
#endif
        return inter_size() + (this->egptr() - this->gptr())
                + (this->pptr() - this->pbase());
    }

    // Get a list of buffers that represents the input sequence.
    const_buffers_type data() const {
#if defined(YDEBUG)
        std::cerr << "DATA ()\n";
#endif
        const_buffers_type cb(allocator_);
        fragment_list_const_iterator iter = get_active();

        std::size_t sz;

        if (iter == put_active()) {
            sz = this->pptr() - this->gptr();
        } else {
            sz = (*iter)->end() - this->gptr();
        }

        // add first fragment, but avoid to add empty buffers
        if (sz > 0) {
            cb.push_back(asio::const_buffer(this->gptr(), sz));
        }

        if (iter != put_active()) {
            // add everithing until active put segment
            while (++iter != put_active()) {
                sz = (*iter)->size();

                if (sz > 0)
                    cb.push_back(
                            asio::const_buffer(
                                    detail::buffer_cast<void const*>(**iter),
                                    sz));
            }

            // add put_active segment
            char_type const* ptr = detail::buffer_cast<char_type const*>(**iter);
            std::size_t sz = this->pptr() - ptr;

            if (sz > 0) {
                cb.push_back(asio::const_buffer(ptr, sz));
            }
        }

        return cb;
    }

    // Get a list of buffers that represents the output sequence, with the given size.
    mutable_buffers_type prepare(std::size_t size) {
#if defined(YDEBUG)
        std::cerr << "PREPARE (" << size << ")\n";
        PRINT_DEBUG (std::cerr);
#endif
        reserve(size);

        mutable_buffers_type mb(allocator_);

        fragment_list_const_iterator iter = put_active();

        std::size_t sz;
        if ((sz = (*iter)->end() - this->pptr()) > 0) {
            mb.push_back(asio::mutable_buffer(this->pptr(), sz));
        }

        while (++iter != fragments_.end()) {
            if ((sz = (*iter)->size()) > 0) {
                mb.push_back(asio::mutable_buffer((*iter)->begin(), sz));
            }
        }

        return mb;
    }

// Remove characters from the input sequence.
    void consume(std::size_t size) {
#if defined(YDEBUG)
        std::cerr << "CONSUME (" << size << "), tailsize=" << tail_size ()
        << ", putsize=" << put_size () << "\n";
        PRINT_DEBUG (std::cerr);
#endif

        // TO THINK: should it be accepted?
        if (!size) {
            return;
        }

        assert(size > 0 && size <= this->size());

        compact_put_area();

        if (get_active() == put_active()) {
            extend_get_area();
        } else {
            // FIXME: not optimal
            std::streambuf::setg(this->eback(), this->gptr(), (*get_active())->end());
        }

        if (size <= this->egptr() - this->gptr()) {
            std::streambuf::setg(this->gptr() + size, this->gptr() + size,
                    this->egptr());
            this->size();
            return;
        }

        inter_size_ -= (size - (this->egptr() - this->gptr()));

        std::size_t n = std::min<std::size_t>(size,
                this->egptr() - this->gptr());

        size -= n;

        while (size > 0) {
            fragments_.pop_front();

            if (size > (*get_active())->size()) {
                size -= (*get_active())->size();
            } else {
                std::streambuf::setg((*get_active())->begin() + size,
                        (*get_active())->begin() + size,
                        (get_active() == put_active()) ?
                                this->pbase() : (*get_active())->end());
                size = 0;
            }
        }

#if defined(YDEBUG)
        this->size ();
        std::cerr << "CONSUME - OK, tailsize=" << tail_size ()
        << ", putsize=" << put_size () << "\n";
#endif
    }

    // advance put pointer to n bytes
    void commit(std::size_t n) {
#if defined(YDEBUG)
        std::cerr << "COMMIT (" << n << "), put_size=" << put_size ()
        << ", tail_size=" << tail_size ()
        << "\n";
        PRINT_DEBUG (std::cerr);
#endif
        assert(n <= put_size() && "not enough space in put area");

        // the current [pbase-epptr] area is large enough
        if (fragment_put_size() >= n) {
            this->pbump(static_cast<int>(n));
#if defined(YDEBUG)
            std::cerr << "COMMIT () - OK, put_size=" << put_size ()
            << ", tail_size=" << tail_size ()
            << "\n";
#endif
            promote_put_if_exist();
            return;
        }

#if defined(YDEBUG)
        std::cerr << "COMMIT_1, putsize=" << put_size () << "\n";
#endif
        n -= fragment_put_size();

        // should always be zero
        // tail_size_ -= ((*put_active ())->end () - this->epptr ());
        assert((*put_active())->end() - this->epptr() == 0);

#if defined (YDEBUG)
        std::cerr << "COMMIT_2: intersize: " <<
        inter_size_ << " += " <<
        ((*put_active ())->end () - this->pbase ()) << "\n";
#endif
        inter_size_ += std::size_t((*put_active())->end() - this->pbase());
        ++put_active_;
        assert(put_active_ != fragments_.end());

        std::size_t sz;
        while ((sz = (*put_active())->size()) < n) {
#if defined (YDEBUG)
            std::cerr << "CONSUME_3: intersize: " <<
            inter_size_ << " += " << sz << "\n";
#endif
            n -= sz;
            tail_size_ -= sz;
            inter_size_ += sz;
            ++put_active_;
            assert(put_active_ != fragments_.end());
        }
#if defined(YDEBUG)
        std::cerr << "COMMIT_4, putsize=" << put_size () << ", tail_size=" << tail_size_ << ", buffer_size=" << (*put_active ())->size() << "\n";
#endif

        inter_size_ += n;
        tail_size_ -= (*put_active())->size();
        std::streambuf::setp((*put_active())->begin() + n, (*put_active())->end());
        promote_put_if_exist();
#if defined(YDEBUG)
        PRINT_DEBUG (std::cerr);
        std::cerr << "COMMIT () - OK, putsize=" << put_size () << "\n";
#endif
    }

protected:
    const fragment_list & fragments() const {
        return fragments_;
    }

    int overflow (int c)
    {
#if defined(YDEBUG)
      std::cerr << "OVERFLOW (" << c << "), put ptrs: "
                << (void*) this->pbase () << ", "
                << (void*) this->pptr () << ", "
                << (void*) this->epptr ()
                << "\n";
      PRINT_DEBUG (std::cerr);
#endif

      reserve(1);

      if (fragment_put_size () == 0)
        // advance put area to next fragment
        promote_put ();
      
      assert(this->pptr() && fragment_put_size () != 0);
      if (traits_type::eof() != c)
      {
        *this->pptr()=traits_type::to_char_type(c);
      	this->pbump(1);
     	}
      return c;
    }

    void promote_put_if_exist() {
        if (fragment_put_size () > 0
                || (&(*put_active_) == &fragments_.back())) {
            return;
        }
        promote_put();
    }

    /**
     * Limits specified desired_size to the fragment size constrains.
     */
    std::size_t constrain_fragment_size( std::size_t desired_size ) const {
        return std::min(std::max(desired_size, min_fragmentation_), max_fragmentation_);
    }
    // Extend put area to n bytes (total)
    void reserve(std::size_t n) {
#if defined(YDEBUG)
        std::cerr << "RESERVE (" << n << "), put_size=" << put_size () << "\n";
#endif

        if (total_size_ + n > max_size_) {
            std::length_error ex("y::zerocopy::streambuf too long");
            boost::throw_exception(ex);
        }

        if (put_size() >= n) {
            return;
        }

#if defined(YDEBUG)
        std::cerr << "RESERVE (" << n << ") - 0\n";
#endif
        while (put_size() < n) {
            const std::size_t needed_size = n - put_size();
            add_fragment(constrain_fragment_size(needed_size));

        }

#if defined(YDEBUG)
        std::cerr << "RESERVE (" << n << ") - OK\n";
#endif
    }

    // Fix put and (if needed) get end and base pointers
    bool compact_put_area() {
#if defined(YDEBUG)
        std::cerr << "COMPACT_PUT_AREA ()\n";
#endif
        if (this->pptr() == this->pbase()) {
            return false;
        }

#if defined(YDEBUG)
        std::cerr << "COMPACT_PUT_AREA (): intersize: " <<
        inter_size_ << " += " <<
        (this->pptr () - this->pbase ()) << "\n";
#endif
        inter_size_ += std::size_t(this->pptr() - this->pbase());

        // move pbase pointer to pptr
        std::streambuf::setp(this->pptr(), this->epptr());

        return true;
    }

    bool extend_get_area() {
#if defined(YDEBUG)
        std::cerr << "EXTEND_GET_AREA ()\n";
#endif
        assert(get_active() == put_active());

        if (this->egptr() == this->pbase()) {
            return false;
        }

#if defined(YDEBUG)
        std::cerr << "EXTEND_GET_AREA (): intersize: " <<
        inter_size_ << " -= " <<
        (this->pbase () - this->egptr ()) << "\n";
#endif
        inter_size_ -= std::size_t(this->pbase() - this->egptr());

        // extend egptr pointer to pbase
        std::streambuf::setg(this->eback(), this->gptr(), this->pbase());

        return true;
    }

    // streambuf 'put' interface
    std::streamsize xsputn(char_type const* s, std::streamsize n) {
#if defined(YDEBUG)
        std::cerr << "XSPUTN (" << n << "): put ptrs: "
        << (void*) this->pbase () << ", "
        << (void*) this->pptr () << ", "
        << (void*) this->epptr () << ", put_size=" << put_size ()
        << "\n";
#endif

        std::streamsize size = n;

        // allocate space if not already
        reserve(std::size_t(size));

        while (size > 0) {
            if (fragment_put_size() == 0) {
                // advance put area to next fragment
                promote_put();
            }

            std::size_t frag = std::min(fragment_put_size(), std::size_t(size));

            std::memcpy(this->pptr(), s, frag);

            s += frag;
            size -= frag;

            commit(frag);
        }

#if defined(YDEBUG)
        std::cerr << "XSPUTN () - OK, put ptrs: "
        << (void*) this->pbase () << ", "
        << (void*) this->pptr () << ", "
        << (void*) this->epptr ()
        << "\n";
        PRINT_DEBUG (std::cerr);
#endif

        return n;
    }

    void update_put() {
        std::streambuf::setp((*put_active())->begin(), (*put_active())->end());
        tail_size_ -= std::size_t(this->epptr() - this->pbase());
    }

    // advance put active fragment
    void promote_put() {
#if defined(YDEBUG)
        std::cerr << "PROMOTE_PUT ()\n";
#endif
        assert(this->epptr() == (*put_active())->end());
        assert(fragment_put_size() == 0);

        // make sure it will be at least one free fragment
        // after put_active
        reserve(1);

        // should always be zero
        // tail_size_ -= ((*put_active ())->end () - this->epptr ());
        assert((*put_active())->end() - this->epptr() == 0);

#if defined (YDEBUG)
        std::cerr << "PROMOTE_PUT: intersize: " <<
        inter_size_ << " += " <<
        ((*put_active ())->end () - this->pbase ()) << "\n";
#endif
        inter_size_ += std::size_t((*put_active())->end() - this->pbase());
        ++put_active_;

        update_put();
#if defined(YDEBUG)
        std::cerr << "PROMOTE_PUT () - OK\n";
        PRINT_DEBUG (std::cerr);
#endif
    }

    std::streamsize xsgetn(char_type* s, std::streamsize n) {
#if defined(YDEBUG)
        std::cerr << "XSGETN (" << n << ")\n";
#endif

        std::streamsize ret = 0;
        while (n > 0) {
            std::streamsize sz = std::min(n, this->egptr() - this->gptr());
            std::memcpy(s, this->gptr(), std::size_t(sz));
            s += sz;
            ret += sz;
            n -= sz;
            std::streambuf::setg(this->eback(), this->gptr() + sz,
                    this->egptr());

            if (n > 0 && underflow() == streambuf_type::traits_type::eof()) {
                break;
            }
        }

#if defined(YDEBUG)
        std::cerr << "XSGETN = " << ret << "\n";
#endif
        return ret;
    }

    typename streambuf_type::traits_type::int_type underflow() {
#if defined(YDEBUG)
        std::cerr << "UNDERFLOW (): get ptrs:  "
        << (void*) this->eback () << ", "
        << (void*) this->gptr () << ", "
        << (void*) this->egptr ()
        << "\n";
#endif

        if (get_active() == put_active()) {
#if defined(YDEBUG)
            std::cerr << "UNDERFLOW_1\n";
#endif
            if (this->gptr() == this->pptr()) {
                return streambuf_type::traits_type::eof();
            }

            compact_put_area();
            extend_get_area();
        } else if (this->egptr() != (*get_active())->end()) {
#if defined(YDEBUG)
            std::cerr << "UNDERFLOW_2\n";

            std::cerr << "UNDERFLOW (): intersize: " <<
            inter_size_ << " -= " <<
            ((*get_active ())->end () - this->egptr ()) << "\n";
#endif
            inter_size_ -= std::size_t((*get_active())->end() - this->egptr());

            // extend egptr pointer to end of fragment
            std::streambuf::setg(this->eback(), this->gptr(),
                    (*get_active())->end());

        } else {
#if defined(YDEBUG)
            std::cerr << "UNDERFLOW_3\n";
#endif
            fragments_.pop_front();
            char_type* eptr;

            if (get_active() == put_active()) {
                // FIXME: compact put area on demand only?
                // if ((*get_active ())->begin () == this->pbase ())
                compact_put_area();

                eptr = this->pbase();
            } else {
                eptr = (*get_active())->end();
            }

#if defined (YDEBUG)
            std::cerr << "UNDERFLOW_3 (): intersize: " <<
            inter_size_ << " -= " <<
            (eptr - (*get_active ())->begin ()) << "\n";
#endif
            inter_size_ -= std::size_t(eptr - (*get_active())->begin());
            std::streambuf::setg((*get_active())->begin(),
                    (*get_active())->begin(), eptr);
        }

#if defined(YDEBUG)
        std::cerr << "UNDERFLOW () - OK: get ptrs:  "
        << (void*) this->eback () << ", "
        << (void*) this->gptr () << ", "
        << (void*) this->egptr ()
        << "\n";
#endif

        return (this->gptr() == this->egptr()) ?
                streambuf_type::traits_type::eof() : *this->gptr();
    }

    typename streambuf_type::traits_type::int_type pbackfail(
            typename streambuf_type::traits_type::int_type /*c*/=
                    streambuf_type::traits_type::eof()) {
#if defined(YDEBUG)
        std::cerr << "PBACKFAIL\n";
#endif
        return streambuf_type::traits_type::eof();
    }
};

template<typename C, typename T, typename F, typename A>
const std::size_t basic_streambuf<C, T, F, A>::default_max_size = (std::numeric_limits<std::size_t>::max)();

template<typename C, typename T, typename F, typename A>
const std::size_t basic_streambuf<C, T, F, A>::default_max_fragmentation = 1024 * 1024;

template<typename C, typename T, typename F, typename A>
const std::size_t basic_streambuf<C, T, F, A>::default_min_fragmentation = 512;

typedef basic_streambuf<> streambuf;

YAMAIL_FQNS_DATA_ZC_END

#endif // _YPLATFORM_ZEROCOPY_STREAMBUF_H_
