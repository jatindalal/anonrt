#pragma once

#include "imgui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_imgui.h"
#include "sokol_log.h"
#include "tinyfiledialogs.h"

#include "ring_buffer.h"
#include "video_input.h"
#include <chrono>
#include <cstring>
#include <memory>
#include <opencv2/opencv.hpp>
#include <thread>

class App {
public:
	static sapp_desc make_desc(int, char *[])
	{
		static App instance;
		return (sapp_desc) {
			.init_userdata_cb = [](void *ud) { static_cast<App *>(ud)->init(); },
			.frame_userdata_cb = [](void *ud) { static_cast<App *>(ud)->frame(); },
			.cleanup_userdata_cb = [](void *ud) { static_cast<App *>(ud)->cleanup(); },
			.event_userdata_cb = [](const sapp_event *e, void *ud) { static_cast<App *>(ud)->event(e); },
			.user_data = &instance,
			.width = 600,
			.height = 400,
			.window_title = "anonrt",
			.icon.sokol_default = true,
			.logger.func = slog_func,
			.high_dpi = true,
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
		m_pass_action.colors[0].clear_value = (sg_color) { 0.1f, 0.1f, 0.1f, 1.0f };
	}

	void frame()
	{
		simgui_frame_desc_t frame_desc { 0 };
		frame_desc.width = sapp_widthf();
		frame_desc.height = sapp_heightf();
		frame_desc.delta_time = sapp_frame_duration();
		frame_desc.dpi_scale = sapp_dpi_scale();
		simgui_new_frame(&frame_desc);

		consume_frame();

		ImGui::Begin("Anonrt");
		ImGui::Text("Video Anonymizer");
		ImGui::Separator();
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
				restart_input(VideoInput::VideoType::File, name);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Open Webcam")) {
			restart_input(VideoInput::VideoType::Webcam, "");
		}
		ImGui::Text("Blur Strength");
		ImGui::SliderFloat("##blur", &m_blur_strength, 1.0f, 50.0f);
		ImGui::End();

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
		m_buffer.flush();

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

			m_buffer.push(std::move(frame));
		}
	}

	void consume_frame()
	{
		auto latest_or_empty = m_buffer.pop();
		if (latest_or_empty.has_value()) {
			update_image(std::move(latest_or_empty.value()));
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
		// rgba = resize_to_window(rgba);

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

	cv::Mat resize_to_window(const cv::Mat &src)
	{
		int win_w = sapp_width();
		int win_h = sapp_height();

		cv::Mat dst;
		cv::resize(src, dst, cv::Size(win_w, win_h), 0, 0, cv::INTER_LINEAR);
		return dst;
	}

	sg_pass_action m_pass_action;
	float m_blur_strength = 15.0f;
	RingBuffer<cv::Mat, 10> m_buffer;
	std::unique_ptr<VideoInput> m_video_input;
	std::unique_ptr<std::jthread> m_producer_thread;

	sg_image m_frame_image { 0 };
	sg_view m_frame_view { 0 };
	unsigned int m_image_w { 0 }, m_image_h { 0 };
};
