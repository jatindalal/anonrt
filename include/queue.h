#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>

template <typename T>
class ThreadSafeQueue {
public:
	ThreadSafeQueue(size_t max_size)
		: m_max_size(max_size)
	{
		if (!max_size)
			throw std::runtime_error("Invalid queue size!");
	}
	ThreadSafeQueue(const ThreadSafeQueue &) = delete;
	ThreadSafeQueue &operator=(const ThreadSafeQueue &) = delete;

	void push(T &&value)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_queue.size() == m_max_size) {
			m_queue.pop_front();
		}
		m_queue.push_back(std::move(value));
	}

	void push(const T &value)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_queue.size() == m_max_size) {
			m_queue.pop_front();
		}
		m_queue.push_back(value);
	}

	std::optional<T> pop()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_queue.empty()) {
			return { };
		}

		T value = std::move(m_queue.front());
		m_queue.pop_front();
		return value;
	}

	size_t size()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_queue.size();
	}

	bool empty()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_queue.empty();
	}

private:
	size_t m_max_size;
	std::deque<T> m_queue;
	mutable std::mutex m_mutex;
};
