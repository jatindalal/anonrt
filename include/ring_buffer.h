#pragma once

#include <array>
#include <cstddef>
#include <mutex>
#include <optional>

template <typename T, size_t SIZE>
class RingBuffer {
public:
	RingBuffer() = default;
	RingBuffer(const RingBuffer &) = delete;
	RingBuffer &operator=(const RingBuffer &) = delete;

	void push(T &&value)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		m_buffer[m_head] = std::move(value);

		m_head = next(m_head);
		if (m_size == SIZE) {
			m_tail = next(m_tail);
			++m_overwrite_count;
		} else {
			++m_size;
		}
		++m_push_count;
	}

	std::optional<T> pop()
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		if (m_size == 0) {
			return { };
		}

		T value = std::move(m_buffer[m_tail]);
		m_tail = next(m_tail);
		--m_size;
		++m_pop_count;
		return std::move(value);
	}

	size_t size() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_size;
	}

	bool empty() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_size == 0;
	}

	void flush()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_head = m_tail = m_size = 0;
		m_push_count = m_pop_count = m_overwrite_count = 0;
	}
private:
	constexpr size_t next(size_t i) const
	{
		return (i + 1) % SIZE;
	}
	std::array<T, SIZE> m_buffer;
	size_t m_head { 0 }, m_tail { 0 }, m_size { 0 };
	size_t m_push_count { 0 }, m_pop_count { 0 }, m_overwrite_count { 0 };
	mutable std::mutex m_mutex;
};

