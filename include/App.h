#pragma once

#include "imgui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_imgui.h"
#include "sokol_log.h"
#include "tinyfiledialogs.h"

#include "anonymizer.h"
#include "queue.h"
#include "ring_buffer.h"
#include "video_input.h"
#include <chrono>
#include <cstring>
#include <limits>
#include <memory>
#include <opencv2/opencv.hpp>
#include <thread>

class App {
public:
	static sapp_desc make_desc(int, char *[])
	{
		static App instance;
		return sapp_desc{
			.user_data = &instance,
			.init_userdata_cb = [](void *ud) { static_cast<App *>(ud)->init(); },
			.frame_userdata_cb = [](void *ud) { static_cast<App *>(ud)->frame(); },
			.cleanup_userdata_cb = [](void *ud) { static_cast<App *>(ud)->cleanup(); },
			.event_userdata_cb = [](const sapp_event *e, void *ud) { static_cast<App *>(ud)->event(e); },
			.width = 600,
			.height = 400,
			.high_dpi = true,
			.window_title = "anonrt",
			.icon{
				.sokol_default = true,
			},
			.logger{
				.func = slog_func,
			},
		};
	}

private:
	void init()
	{
		sg_desc desc { 0 };
		desc.environment = sglue_environment();
		desc.logger.func = slog_func;
		sg_setup(&desc);

		simgui_desc_t imgui_desc { 0 };
		simgui_setup(&imgui_desc);

		m_pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
		m_pass_action.colors[0].clear_value = sg_color{ 0.1f, 0.1f, 0.1f, 1.0f };
	}

	void frame()
	{
		simgui_frame_desc_t frame_desc { 0 };
		frame_desc.width = sapp_widthf();
		frame_desc.height = sapp_heightf();
		frame_desc.delta_time = sapp_frame_duration();
		frame_desc.dpi_scale = sapp_dpi_scale();
		simgui_new_frame(&frame_desc);

		ImGui::Begin("Anonrt");
		ImGui::SeparatorText("Video Anonymizer");
		ImGui::BeginDisabled(m_recording_active.load());
		if (ImGui::Button("Open Video")) {
			const char *filters[] = {
				"*.mp4", "*.mov", "*.avi", "*.mkv", "*.webm", "*.m4v", "*.wmv", "*.flv"
			};
			const char *name = tinyfd_openFileDialog("Select Video File",
				"",
				0,
				filters,
				"Video Files",
				0);

			if (name) {
				m_video_file_name = name;
				restart_input(VideoInput::VideoType::File, m_video_file_name.c_str());
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Open Webcam")) {
			m_video_file_name.clear();
			restart_input(VideoInput::VideoType::Webcam, "");
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset")) {
			m_video_file_name.clear();
			if (m_producer_thread) {
				m_producer_thread->request_stop();
				m_producer_thread->join();
				m_producer_thread.reset(nullptr);
			}
			if (m_video_input) {
				m_video_input.reset(nullptr);
			}
			if (m_frame_view.id != 0) {
				sg_destroy_view(m_frame_view);
			}
			if (m_frame_image.id != 0) {
				sg_destroy_image(m_frame_image);
			}
			m_frame_view = sg_view { 0 };
			m_frame_image = sg_image { 0 };
			m_image_w = 0;
			m_image_h = 0;
			sg_image_data data { 0 };
			sg_update_image(m_frame_image, &data);
			m_ui_buffer.flush();
		}
		ImGui::EndDisabled();
		ImGui::Text("Anonymize");
		ImGui::SameLine();
		ImGui::Checkbox("##Anonymize", &m_anonymize);
		if (ImGui::SliderFloat("Score Threshold", &m_anonymizer_score_threshold, 0.0, 1.0)) {
			m_anonymizer.set_score_threshold(m_anonymizer_score_threshold);
		}
		if (ImGui::SliderFloat("NMS threshold", &m_anonymizer_nms_threshold, 0.0, 1.0)) {
			m_anonymizer.set_score_threshold(m_anonymizer_nms_threshold);
		}
		if (ImGui::SliderInt("Top K", &m_anonymizer_top_k, 3000, 6000)) {
			m_anonymizer.set_top_k(m_anonymizer_top_k);
		}
		if (ImGui::SliderInt("Block Size", &m_anonymizer_block_size, 6, 20)) {
			m_anonymizer.set_block_size(m_anonymizer_block_size);
		}

		ImGui::SeparatorText("Recording");
		ImGui::BeginDisabled(m_recording_active.load());
		if (m_save_path.empty()) {
			if (ImGui::Button("Choose")) {
				const char *filter[] = { ".mp4" };
				std::string save_path = tinyfd_saveFileDialog("Save File", "untitled.mp4", 1,
					filter, "Video Files");
				if (!save_path.empty()) {
					m_save_path = save_path;
				}
			}
		} else {
			if (ImGui::Button("Clear")) {
				m_save_path.clear();
			}
		}
		ImGui::SameLine();
		ImGui::Text("Save Path: %s", m_save_path.c_str());
		ImGui::EndDisabled();

		ImGui::BeginDisabled(m_save_path.empty() || !m_video_input || m_recording_active.load());
		if (ImGui::Button("Save")) {
			m_recording_active.store(true);
			bool is_video = m_video_input->get_type() == VideoInput::VideoType::File;
			if (is_video) {
				m_recording_thread = std::make_unique<std::jthread>([this](std::stop_token st) {
					save_video_file(st);
				});
			} else {
				m_fill_save_buffer.store(true);
				m_recording_thread = std::make_unique<std::jthread>([this](std::stop_token st) {
					save_video_webcam(st);
				});
			}
		}
		ImGui::EndDisabled();
		if (m_recording_active.load() && m_video_input->get_type() == VideoInput::VideoType::Webcam) {
			if (ImGui::Button("Stop Recording")) {
				m_recording_thread->request_stop();
			}
		}
		ImGui::End();

		consume_frame();

		sg_pass pass { 0 };
		pass.action = m_pass_action;
		pass.swapchain = sglue_swapchain();
		sg_begin_pass(&pass);
		simgui_render();
		sg_end_pass();
		sg_commit();
	}

	void cleanup()
	{
		if (m_recording_thread) {
			m_recording_thread->request_stop();
			m_recording_thread->join();
		}
		if (m_producer_thread) {
			m_producer_thread->request_stop();
			m_producer_thread->join();
		}
		simgui_shutdown();
		sg_shutdown();
	}

	void event(const sapp_event *event)
	{
		simgui_handle_event(event);

		if (event->type == SAPP_EVENTTYPE_KEY_DOWN
			&& event->key_code == SAPP_KEYCODE_Q
			&& (event->modifiers & SAPP_MODIFIER_SUPER)) {
			sapp_quit();
		}
	}

	void restart_input(VideoInput::VideoType type, const char *name)
	{
		if (m_producer_thread) {
			m_producer_thread->request_stop();
			m_producer_thread->join();
			m_producer_thread.reset(nullptr);
		}
		if (m_video_input) {
			m_video_input.reset(nullptr);
		}
		m_video_input = std::make_unique<VideoInput>();

		switch (type) {
		case VideoInput::VideoType::Webcam: {
			m_video_input->open(0);
			break;
		}
		case VideoInput::VideoType::File: {
			m_video_input->open(name);
			break;
		}
		default:
			throw std::runtime_error("Invalid type");
		}

		m_producer_thread = std::make_unique<std::jthread>([this](std::stop_token st) {
			produce(st);
		});
	}

	void produce(std::stop_token stoken)
	{
		m_ui_buffer.flush();

		double fps = m_video_input->get_fps();
		bool is_video = m_video_input->get_type() == VideoInput::VideoType::File;
		double frame_interval = is_video ? (1.0 / fps) : 0.0;
		auto next_deadline = std::chrono::steady_clock::now();

		while (!stoken.stop_requested()) {
			cv::Mat frame;
			if (!m_video_input->get_frame(frame)) {
				break;
			}

			if (is_video) {
				next_deadline += std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(frame_interval));
				std::this_thread::sleep_until(next_deadline);
			}

			if (m_anonymize) {
				m_anonymizer.anonymize(frame);
			}
			if (m_fill_save_buffer.load()) {
				m_save_buffer.push(cv::Mat(frame));
			}
			m_ui_buffer.push(std::move(frame));
		}
	}

	void consume_frame()
	{
		auto latest_or_empty = m_ui_buffer.pop();
		if (latest_or_empty.has_value()) {
			auto value = std::move(latest_or_empty.value());
			update_image(std::move(value));
		}

		if (m_frame_image.id == 0) {
			return;
		}

		ImVec2 win_size = ImGui::GetIO().DisplaySize;
		float aspect = (float)m_image_w / (float)m_image_h;
		float win_aspect = win_size.x / win_size.y;

		ImVec2 draw_size;
		if (win_aspect > aspect) {
			draw_size.y = win_size.y;
			draw_size.x = win_size.y * aspect;
		} else {
			draw_size.x = win_size.x;
			draw_size.y = win_size.x / aspect;
		}
		ImVec2 pos((win_size.x - draw_size.x) * 0.5f, (win_size.y - draw_size.y) * 0.5f);

		ImGui::GetBackgroundDrawList()->AddImage(
			simgui_imtextureid(m_frame_view),
			pos,
			ImVec2(pos.x + draw_size.x, pos.y + draw_size.y));
	}

	void update_image(cv::Mat &&bgr_image)
	{
		if (bgr_image.empty()) {
			return;
		}

		cv::Mat rgba;
		cv::cvtColor(bgr_image, rgba, cv::COLOR_BGR2RGBA);

		if (rgba.cols != m_image_w || rgba.rows != m_image_h) {
			if (m_frame_view.id != 0) {
				sg_destroy_view(m_frame_view);
			}
			if (m_frame_image.id != 0) {
				sg_destroy_image(m_frame_image);
			}

			sg_image_desc desc { 0 };
			desc.width = rgba.cols;
			desc.height = rgba.rows;
			desc.pixel_format = SG_PIXELFORMAT_RGBA8;
			desc.usage.stream_update = true;
			m_frame_image = sg_make_image(&desc);

			sg_view_desc view_desc { 0 };
			view_desc.texture.image = m_frame_image;
			m_frame_view = sg_make_view(&view_desc);

			m_image_w = rgba.cols;
			m_image_h = rgba.rows;
		}

		sg_image_data data { 0 };
		data.mip_levels[0].ptr = rgba.data;
		data.mip_levels[0].size = rgba.total() * rgba.elemSize();
		sg_update_image(m_frame_image, &data);
	}

	void save_video_file(std::stop_token stoken)
	{
		auto fps = m_video_input->get_fps();
		auto frame_w = m_video_input->get_w();
		auto frame_h = m_video_input->get_h();

		VideoInput video_file;
		Anonymizer anonymizer{ "/Users/jd/hax/anonrt/face_detection_yunet_2026may.onnx" };
		video_file.open(m_video_file_name.c_str());
		cv::VideoWriter writer(m_save_path, cv::VideoWriter::fourcc('H', '2', '6', '4'), fps, cv::Size(frame_w, frame_h));
		cv::Mat frame;
		while (video_file.get_frame(frame) && !stoken.stop_requested()) {
			if (frame.empty()) break;
			anonymizer.anonymize(frame);
			writer.write(frame);
		}
		m_recording_active.store(false);
	}

	void save_video_webcam(std::stop_token stoken) {
		auto fps = m_video_input->get_fps();
		auto frame_w = m_video_input->get_w();
		auto frame_h = m_video_input->get_h();
		cv::VideoWriter writer(m_save_path, cv::VideoWriter::fourcc('H', '2', '6', '4'), fps, cv::Size(frame_w, frame_h));
		while (!stoken.stop_requested()) {
			auto frame_or_empty = m_save_buffer.pop();
			if (frame_or_empty.has_value()) {
				auto frame = std::move(frame_or_empty.value());
				if (frame.empty()) continue;
				writer.write(frame);
			}
		}
		m_recording_active.store(false);
		m_fill_save_buffer.store(false);
	}

	sg_pass_action m_pass_action;
	bool m_anonymize = false;
	std::string m_save_path;
	std::atomic<bool> m_recording_active { false };
	std::atomic<bool> m_fill_save_buffer { false };
	RingBuffer<cv::Mat, 30> m_ui_buffer;
	ThreadSafeQueue<cv::Mat> m_save_buffer { std::numeric_limits<size_t>::max() };
	std::string m_video_file_name;

	std::unique_ptr<VideoInput> m_video_input;
	std::unique_ptr<std::jthread> m_producer_thread, m_recording_thread;
	Anonymizer m_anonymizer { "/Users/jd/hax/anonrt/face_detection_yunet_2026may.onnx" };
	int m_anonymizer_block_size { 12 }, m_anonymizer_top_k { 5000 };
	float m_anonymizer_score_threshold { 0.7 }, m_anonymizer_nms_threshold { 0.3 };

	sg_image m_frame_image { 0 };
	sg_view m_frame_view { 0 };
	unsigned int m_image_w { 0 }, m_image_h { 0 };
};
