#pragma once

#include <opencv2/objdetect.hpp>
#include <opencv2/opencv.hpp>

class Anonymizer {
public:
	Anonymizer(const char *model_path)
	{
		m_model = cv::FaceDetectorYN::create(model_path, "", m_input_size,
			m_score_threshold, m_nms_threshold, m_topk);
	}
	Anonymizer(const Anonymizer &) = delete;
	Anonymizer &operator=(const Anonymizer &) = delete;

	void set_input_size(const cv::Size &input_size)
	{
		m_input_size = input_size;
		m_model->setInputSize(m_input_size);
	}

	unsigned int anonymize(const cv::Mat &frame)
	{
		if (frame.size() != m_input_size) {
			set_input_size(frame.size());
		}

		cv::Mat faces;
		m_model->detect(frame, faces);

		for (int i = 0; i < faces.rows; i++) {
			int x = std::max(0, (int)faces.at<float>(i, 0));
			int y = std::max(0, (int)faces.at<float>(i, 1));
			int w = std::min((int)faces.at<float>(i, 2), frame.cols - x);
			int h = std::min((int)faces.at<float>(i, 3), frame.rows - y);
			if (w <= 0 || h <= 0)
				continue;

			cv::Rect roi(x, y, w, h);
			pixelate(frame(roi));
		}

		return faces.rows;
	}

	void set_score_threshold(float new_threshold)
	{
		m_model->setScoreThreshold(new_threshold);
	}

	void set_nms_threshold(float nms_threshold)
	{
		m_model->setNMSThreshold(nms_threshold);
	}

	void set_top_k(unsigned int top_k) {
		m_model->setTopK(top_k);
	}

	void set_block_size(unsigned int block_size) {
		m_block_size = block_size;
	}

private:
	void pixelate(cv::Mat roi)
	{
		cv::Mat small;
		cv::resize(roi, small,
				   cv::Size(std::max(1, roi.cols / static_cast<int>(m_block_size)),
				   std::max(1, roi.rows / static_cast<int>(m_block_size))),
				   0, 0, cv::INTER_LINEAR);

		cv::resize(small, roi, roi.size(), 0, 0, cv::INTER_NEAREST);
	}

	cv::Size m_input_size { 640, 640 };
	cv::Ptr<cv::FaceDetectorYN> m_model;
	float m_score_threshold { 0.7f }, m_nms_threshold { 0.3f };
	unsigned int m_topk { 5000 }, m_block_size { 12 };
};
