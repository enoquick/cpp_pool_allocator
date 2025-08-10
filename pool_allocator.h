#ifndef _POOL_ALLOCATOR_H
#define _POOL_ALLOCATOR_H

#include <cassert>
#include <memory>
#include <cstdlib>
#include <mutex>

#ifdef _MSC_VER 
#pragma warning(push)
#pragma warning(disable: 26110)
#pragma warning(disable: 26115)
#pragma warning(disable: 26135)
#pragma warning(disable: 26457)
#pragma warning(disable: 26471)
#pragma warning(disable: 26472)
#pragma warning(disable: 26481)
#pragma warning(disable: 26490)
#pragma warning(disable: 26497)
#endif 

template <typename T,bool THREAD_SAFE=true>
class pool_allocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using void_pointer = void*;
    using const_void_pointer = const void*;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
	using propagate_on_container_copy_assignment = std::true_type;
	using propagate_on_container_move_assignment = std::true_type;
	using propagate_on_container_swap = std::true_type;
	using is_always_equal = std::true_type;
	static constexpr size_type default_block_size=256;
	static constexpr bool thread_safe = THREAD_SAFE;
	template<typename _U>
	struct rebind {
		using other = pool_allocator<_U,thread_safe>;
	};

	 pool_allocator(size_type block_size=default_block_size)noexcept
	: _m_block_size(_compute_block_size(block_size))
	{
		_inc_shared_counter();
	}
	 pool_allocator(const pool_allocator& _other)noexcept
		: _m_block_size(_other._m_block_size)
	{
		_inc_shared_counter();
	}
	template<typename _U,bool _S>
	 pool_allocator(const pool_allocator<_U,_S>& _other)noexcept
	: _m_block_size(_compute_block_size(_other._m_block_size))
	{
		_inc_shared_counter();
	}
	 pool_allocator& operator=(const pool_allocator& _other)noexcept {
		_m_block_size = _other._m_block_size;
		return *this;
	}
	 pool_allocator(pool_allocator&& _other)noexcept
	: _m_block_size(_other._m_block_size)
	{
		_inc_shared_counter();
	}
	 pool_allocator& operator=(pool_allocator&& _other)noexcept {
		_m_block_size = _other._m_block_size;
		return *this;
	}
	~pool_allocator() {
		_dec_shared_counter();
	}
	 [[nodiscard]] pointer allocate(size_type n) {
		if (n) [[likely]] {
			[[maybe_unused]] const auto _l = _lock();
			if (auto _p = _acquire(n)) {
				return _p;
			}
		}
		else {
			return {};
		}
		throw std::bad_alloc();
	}
	 void deallocate(pointer p, size_t n)noexcept {
		auto _founded=true;
		if (p && n) {
			[[maybe_unused]] const auto _l = _lock();
			_founded = _release(p, n);
		}
		assert(_founded); //if not found must be an user error 
	}
	 void swap(pool_allocator& _other)noexcept {
		std::swap(_m_block_size, _other._m_block_size);
	}
	template<typename U, typename... Args>
	void construct(U* p, Args&&... args)noexcept(noexcept(std::construct_at(p, std::forward<Args>(args)...))) {
		std::construct_at(p, std::forward<Args>(args)...);
	}

	template<typename U>
	void destroy(U* p)noexcept(noexcept(std::destroy_at(p))) {
		 std::destroy_at(p);
	}

private:
	class _node final {
	public:
		[[nodiscard]]  static _node* allocate(size_type _block_size)noexcept {
			assert(_block_size);
			const auto _sz = (sizeof(_node) - sizeof(_node::_m_data))  + (sizeof(value_type) * _block_size);
			auto _p = static_cast<_node*>(_aligned_malloc<_node>(_sz));
			if (_p) [[likely]]  {
				std::construct_at(_p, _block_size);
			}
			return _p;
		}
		[[nodiscard]] static _node* deallocate(_node* _p)noexcept {
			assert(_p);
			auto _next = _p->_m_next;
			_aligned_free(_p);
			return _next;
		}
		 void detach(_node* _pred)noexcept {
			 assert(_pred);
			_pred->_m_next = _m_next;
		}
		 void attach(_node*& _head)noexcept {
			_m_next = _head;
			_head = this;
			_m_current_free_index = {};
		}
		 [[nodiscard]] _node* next()noexcept { return _m_next; }
		_node(const _node&) = delete;
		_node& operator=(const _node&) = delete;
		_node(_node&&) = delete;
		_node& operator=(_node&&) = delete;
		 ~_node() = default;
		 explicit _node(size_type _block_size)noexcept
			: _m_block_size(_block_size)
		{}
		[[nodiscard]]  pointer acquire(const size_type _n)noexcept {
			assert(_n && _m_block_size >= _n);
			pointer _p{};
			if (_m_current_free_index <= _m_block_size - _n) [[likely]] {
				const auto _start = reinterpret_cast<pointer>(&_m_data);
				_p = _start + _m_current_free_index;
				_m_use_counter += _n;
				_m_current_free_index += _n;
			}
			return _p;
		}
		[[nodiscard]]  size_type use_counter()const noexcept { return _m_use_counter; }
		[[nodiscard]]  size_type block_size()const noexcept { return _m_block_size; }
		[[nodiscard]]  bool release(const value_type* _p,size_type _n)noexcept {
			const auto _start = reinterpret_cast<const value_type*>(&_m_data);
			if (_p >= _start && _p < _start + _m_block_size) {
				const auto _idx = static_cast<size_type>(_p - _start);
				assert(_m_use_counter >= _n);
				assert(_m_block_size - _idx >= _n);
				_m_use_counter -= _n;
				if (_idx + _n == _m_current_free_index) {
					_m_current_free_index = _idx;
				}
				return true;
			}
			return {};
		}
	private:
		_node* _m_next{};
		size_type _m_block_size;
		size_type _m_use_counter{};
		size_type _m_current_free_index{};
		unsigned char _m_data{}; //value_type raw memory - warning, must be the last field 
	};
	[[nodiscard]]  static constexpr size_type _compute_block_size(size_type _block_size)noexcept {
		constexpr auto _max=static_cast<size_type>(std::numeric_limits<difference_type>::max()) / sizeof(value_type);
		if (!_block_size) [[unlikely]] {
			_block_size=default_block_size;
		}
		else if (_block_size > _max) [[unlikely]] {
			_block_size=_max;
		}
		return _block_size;
	}

	[[nodiscard]]  pointer _acquire(size_type _n)noexcept {
		auto _pnode = _sm_node_used;
		while (_pnode) {
			if (auto _p = _pnode->acquire(_n)) {
				return _p;
			}
			_pnode = _pnode->next();
		}
		//not nodes with free slots exists - acquire a node
		_pnode = _acquire_node(_n);
		if (_pnode) {
			auto _p = _pnode->acquire(_n);
			assert(_p);
			return _p;
		}
		return {};
	}
	[[nodiscard]]  static bool _match_free_node(const _node& _r, size_type _n)noexcept {
		const auto _d = _r.block_size() / _n;
		return _d > 0 && _d < 3; //to avoid wasting space
	}
	[[nodiscard]]  _node* _acquire_node(size_type _n)noexcept {
		assert(_n);
		auto _pnode = _sm_node_freed;
		_node* _pred{};
		while (_pnode) { //search in free list a node 
			if (_n == 1 || _match_free_node(*_pnode,_n)) {
				if (_pred) [[unlikely]] {
					_pnode->detach(_pred);
				}
				else {
					_sm_node_freed = _sm_node_freed->next();
				}
				_pnode->attach(_sm_node_used);
				return _pnode;
			}
			_pred = _pnode;
			_pnode = _pnode->next();
		}
		//not exists a node in free list - allocate
		_pnode = _node::allocate(_n < _m_block_size ?  _m_block_size : _n);
		if (_pnode) [[likely]] {
			_pnode->attach(_sm_node_used);
		}
		return _pnode;
	}
	[[nodiscard]]  static bool _release(value_type* _p, size_type _n)noexcept {
		auto _pnode = _sm_node_used;
		_node* _pred{};
		while (_pnode) {
			if (_pnode->release(_p,_n)) {
				if (!_pnode->use_counter()) { //the node is not used - put the node in free list
					if (_pred) [[unlikely]] {
						_pnode->detach(_pred);
					}
					else {
						_sm_node_used = _sm_node_used->next();
					}
					_pnode->attach(_sm_node_freed);
				}
				return true;
			}
			_pred = _pnode;
			_pnode = _pnode->next();
		}
		return {};
	}
	static void _aligned_free(void* _p)noexcept {
#ifdef _WIN32
		::_aligned_free(_p);
#else
		std::free(_p);
#endif
	}
	template <typename _U>
	[[nodiscard]] static void* _aligned_malloc(std::size_t _n)noexcept {
		assert(_n);
#ifdef _WIN32
		return ::_aligned_malloc(_n, alignof(_U));
#else
		return std::aligned_alloc(alignof(_U), _n);
#endif
	}
	[[nodiscard]] static auto _lock()noexcept {
		if constexpr (thread_safe) {
#ifdef _MSC_VER 
#pragma warning(push)
#pragma warning(disable: 26447)
#endif 
			return std::lock_guard<std::mutex>(_sm_lock._lock); //no throw
#ifdef _MSC_VER 
#pragma warning(pop)
#endif 
		}
		else {
			return int{}; //dummy 
		}
	} 
	static void _inc_shared_counter()noexcept {
		[[maybe_unused]] const auto _l = _lock();
		++_sm_shared_counter;
	}
	static void _dec_shared_counter()noexcept {
		[[maybe_unused]] const auto _l = _lock();
		assert(_sm_shared_counter);
		--_sm_shared_counter;
		if (!_sm_shared_counter && _sm_node_freed) {
			_deallocate_list(_sm_node_freed);
		}
	}
	static void _deallocate_list(_node*& _list)noexcept {
		auto _p = _list;
		while (_p) {
			_p=_node::deallocate(_p);
		}
		_list = {};
	}
	typedef struct _lock_true final {
		_lock_true()noexcept = default;
		std::mutex _lock{};
	} _lock_true_t;
	typedef struct _lock_false final {} _lock_false_t;
	using _lock_t = std::conditional_t<thread_safe, _lock_true, _lock_false>;
	template <typename _U,bool _S>
	friend class pool_allocator;
	size_type _m_block_size{};
	static inline _node* _sm_node_used{}; //list used nodes
	static inline _node* _sm_node_freed{};//list free nodes
	static inline size_type _sm_shared_counter{};
	static inline _lock_t _sm_lock{};
};

template <typename T, bool THREAD_SAFE>
[[nodiscard]] inline bool operator==(const pool_allocator<T, THREAD_SAFE>&, const pool_allocator<T, THREAD_SAFE>&)noexcept {
	return true;
}

template <typename T, bool THREAD_SAFE>
[[nodiscard]] inline bool operator!=(const pool_allocator<T, THREAD_SAFE>& _pl, const pool_allocator<T, THREAD_SAFE>& _pr)noexcept {
	return !(_pl == _pr);
}

#ifdef _MSC_VER 
#pragma warning(pop)
#endif 

#endif
