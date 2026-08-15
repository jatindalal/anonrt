#pragma once

#include <iostream>
#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>
#include <mutex>

class VideoInput {
public:
	enum class VideoType {
		Webcam,
		File
	};
	VideoInput() = default;
	VideoInput(const VideoInput &) = delete;
	VideoInput &operator=(const VideoInput &) = delete;

	bool open(const char *video_path)
	{
		if (!m_capture_device.open(video_path)) {
			std::cerr << "Failed to open capture device!";
			return false;
		}
		m_opened = true;
		m_type = VideoType::File;
		return true;
	}

	bool open(int index = 0)
	{
		if (!m_capture_device.open(index)) {
			std::cerr << "Failed to open capture device!";
			return false;
		}
		m_capture_device.set(cv::CAP_PROP_BUFFERSIZE, 1.0);
		m_opened = true;
		m_type = VideoType::Webcam;
		return true;
	}

	bool get_frame(cv::Mat &output)
	{
		std::lock_guard<std::mutex> lock(m_cap_mutex);
		if (!m_opened) {
			throw std::runtime_error("get_frame called without opening a device!");
		}
		return m_capture_device.read(output);
	}

	double get_fps() const
	{
		std::lock_guard<std::mutex> lock(m_cap_mutex);
		return m_capture_device.get(cv::CAP_PROP_FPS);
	}

	int get_w() const
	{
		std::lock_guard<std::mutex> lock(m_cap_mutex);
		return static_cast<int>(m_capture_device.get(cv::CAP_PROP_FRAME_WIDTH));
	}

	int get_h() const
	{
		std::lock_guard<std::mutex> lock(m_cap_mutex);
		return static_cast<int>(m_capture_device.get(cv::CAP_PROP_FRAME_HEIGHT));
	}

	VideoType get_type() const
	{
		return m_type;
	}

	int get_frame_number() const
	{
		std::lock_guard<std::mutex> lock(m_cap_mutex);
		return static_cast<int>(m_capture_device.get(cv::CAP_PROP_POS_FRAMES));
	}

	bool move_to_frame(unsigned int frame)
	{
		std::lock_guard<std::mutex> lock(m_cap_mutex);
		return m_capture_device.set(cv::CAP_PROP_POS_FRAMES, static_cast<double>(frame));
	}

	int get_frame_count() const
	{
		std::lock_guard<std::mutex> lock(m_cap_mutex);
		return static_cast<int>(m_capture_device.get(cv::CAP_PROP_FRAME_COUNT));
	}

private:
	cv::VideoCapture m_capture_device;
	bool m_opened = false;
	VideoType m_type;
	mutable std::mutex m_cap_mutex;
};
