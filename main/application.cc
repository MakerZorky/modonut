#include "application.h"
#include "board.h"
#include "system_info.h"
#include "ml307_ssl_transport.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "font_awesome_symbols.h"
#include "iot/thing_manager.h"
#include "assets/lang_config.h"
#include "boards/common/button.h"
#include "boards/modo-board/config.h"
#include "boards/common/ws2812_led.h"
#include "boards/common/rc522.h"
#include "boards/common/axp2101.h"
#include <nvs_flash.h>
#include <esp_system.h>

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <esp_app_desc.h>
#include <driver/i2c_master.h>
#include <wifi_station.h>

#define TAG "Application"


static const char* const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "configuring",
    "idle",
    "connecting",
    "listening",
    "speaking",
    "upgrading",
    "activating",
    "fatal_error",
    "invalid_state"
};

Application::Application() {
    event_group_ = xEventGroupCreate();
    background_task_ = new BackgroundTask(4096 * 8);

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            app->OnClockTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    if (background_task_ != nullptr) {
        delete background_task_;
    }
    vEventGroupDelete(event_group_);
}

void Application::CheckNewVersion() {
    auto& board = Board::GetInstance();
    // auto display = board.GetDisplay();
    // Check if there is a new firmware version available
    ota_.SetPostData(board.GetJson());

    const int MAX_RETRY = 10;
    int retry_count = 0;

    while (true) {
        if (!ota_.CheckVersion()) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }
            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", 60, retry_count, MAX_RETRY);
            vTaskDelay(pdMS_TO_TICKS(60000));
            continue;
        }
        retry_count = 0;

        if (ota_.HasNewVersion()) {
            Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "happy", Lang::Sounds::P3_UPGRADE);
            // Wait for the chat state to be idle
            do {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } while (GetDeviceState() != kDeviceStateIdle);

            // Use main task to do the upgrade, not cancelable
            Schedule([this]() {
                SetDeviceState(kDeviceStateUpgrading);
                
                // display->SetIcon(FONT_AWESOME_DOWNLOAD);
                // std::string message = std::string(Lang::Strings::NEW_VERSION) + ota_.GetFirmwareVersion();
                // display->SetChatMessage("system", message.c_str());

                auto& board = Board::GetInstance();
                board.SetPowerSaveMode(false);
                // #if CONFIG_USE_WAKE_WORD_DETECT
                // wake_word_detect_.StopDetection();
                // #endif
                // 预先关闭音频输出，避免升级过程有音频操作
                // 通过Board接口控制音频，而不是直接控制硬件
                board.ShowDeviceState("upgrading");
                
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    audio_decode_queue_.clear();
                }
                background_task_->WaitForCompletion();
                delete background_task_;
                background_task_ = nullptr;
                vTaskDelay(pdMS_TO_TICKS(1000));

                ota_.StartUpgrade([this](int progress, size_t speed) {
                    char buffer[64];
                    snprintf(buffer, sizeof(buffer), "%d%% %zuKB/s", progress, speed / 1024);
                    // display->SetChatMessage("system", buffer);
                });

                // If upgrade success, the device will reboot and never reach here
                // display->SetStatus(Lang::Strings::UPGRADE_FAILED);
                ESP_LOGI(TAG, "Firmware upgrade failed...");
                vTaskDelay(pdMS_TO_TICKS(3000));
                Reboot();
            });

            return;
        }

        // No new version, mark the current version as valid
        ota_.MarkCurrentVersionValid();
        // std::string message = std::string(Lang::Strings::VERSION) + ota_.GetCurrentVersion();
        // display->ShowNotification(message.c_str());
    
        if (ota_.HasActivationCode()) {
            // Activation code is valid
            SetDeviceState(kDeviceStateActivating);
            ShowActivationCode();

            // Check again in 60 seconds or until the device is idle
            for (int i = 0; i < 60; ++i) {
                if (device_state_ == kDeviceStateIdle) {
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            continue;
        }

        SetDeviceState(kDeviceStateIdle);
        // display->SetChatMessage("system", "");
        ResetDecoder();
        PlaySound(Lang::Sounds::P3_SUCCESS);
        // Exit the loop if upgrade or idle
        break;
    }
}

void Application::ShowActivationCode() {
    auto& code = ota_.GetActivationCode();

    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::P3_0},
        digit_sound{'1', Lang::Sounds::P3_1}, 
        digit_sound{'2', Lang::Sounds::P3_2},
        digit_sound{'3', Lang::Sounds::P3_3},
        digit_sound{'4', Lang::Sounds::P3_4},
        digit_sound{'5', Lang::Sounds::P3_5},
        digit_sound{'6', Lang::Sounds::P3_6},
        digit_sound{'7', Lang::Sounds::P3_7},
        digit_sound{'8', Lang::Sounds::P3_8},
        digit_sound{'9', Lang::Sounds::P3_9}
    }};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    // Alert(Lang::Strings::ACTIVATION, message.c_str(), "happy", Lang::Sounds::P3_ACTIVATION);
    vTaskDelay(pdMS_TO_TICKS(1000));
    background_task_->WaitForCompletion();

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert %s: %s [%s]", status, message, emotion);
    // auto display = Board::GetInstance().GetDisplay();
    // display->SetStatus(status);
    // display->SetEmotion(emotion);
    // display->SetChatMessage("system", message);
    if (!sound.empty()) {
        // 只有在音频编解码器可用时才播放声音
        auto codec = Board::GetInstance().GetAudioCodec();
        if (codec) {
            ResetDecoder();
            PlaySound(sound);
        } else {
            ESP_LOGW(TAG, "Audio codec not available, skipping sound playback");
        }
    }
}

void Application::DismissAlert() {
    if (device_state_ == kDeviceStateIdle) {
        // auto display = Board::GetInstance().GetDisplay();
        // display->SetStatus(Lang::Strings::STANDBY);
        // display->SetEmotion("neutral");
        // display->SetChatMessage("system", "");
    }
}

void Application::PlaySound(const std::string_view& sound) {
    // 只有在音频编解码器可用时才播放声音
    if (!opus_decoder_) {
        ESP_LOGW(TAG, "Audio decoder not available, skipping sound playback");
        return;
    }
    
    // The assets are encoded at 16000Hz, 60ms frame duration
    SetDecodeSampleRate(16000, 60);
    const char* data = sound.data();
    size_t size = sound.size();
    for (const char* p = data; p < data + size; ) {
        auto p3 = (BinaryProtocol3*)p;
        p += sizeof(BinaryProtocol3);

        auto payload_size = ntohs(p3->payload_size);
        std::vector<uint8_t> opus;
        opus.resize(payload_size);
        memcpy(opus.data(), p3->payload, payload_size);
        p += payload_size;

        std::lock_guard<std::mutex> lock(mutex_);
        audio_decode_queue_.emplace_back(std::move(opus));
    }
}

void Application::ToggleChatState() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                return;
            }

            SetListeningMode(realtime_chat_enabled_ ? kListeningModeRealtime : kListeningModeAutoStop);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {
        Schedule([this]() {
            protocol_->CloseAudioChannel();
        });
    }
}

void Application::StartListening() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            SetListeningMode(kListeningModeManualStop);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
            SetListeningMode(kListeningModeManualStop);
        });
    }
}

void Application::StopListening() {
    Schedule([this]() {
        if (device_state_ == kDeviceStateListening) {
            protocol_->SendStopListening();
            SetDeviceState(kDeviceStateIdle);
        }
    });
}

void Application::Start() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    /* Setup the audio codec */
    auto codec = board.GetAudioCodec();
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(codec->output_sample_rate(), 1, OPUS_FRAME_DURATION_MS);
    opus_encoder_ = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
    if (realtime_chat_enabled_) {
        ESP_LOGI(TAG, "Realtime chat enabled, setting opus encoder complexity to 0");
        opus_encoder_->SetComplexity(0);
    } else if (board.GetBoardType() == "ml307") {
        ESP_LOGI(TAG, "ML307 board detected, setting opus encoder complexity to 5");
        opus_encoder_->SetComplexity(5);
    } else {
        ESP_LOGI(TAG, "WiFi board detected, setting opus encoder complexity to 3");
        opus_encoder_->SetComplexity(3);
    }

    if (codec->input_sample_rate() != 16000) {
        input_resampler_.Configure(codec->input_sample_rate(), 16000);
        reference_resampler_.Configure(codec->input_sample_rate(), 16000);
    }
    
    codec->Start(); 

    xTaskCreatePinnedToCore([](void* arg) {
        Application* app = (Application*)arg;
        app->AudioLoop();
        vTaskDelete(NULL);
    }, "audio_loop", 4096 * 2, this, 8, &audio_loop_task_handle_, realtime_chat_enabled_ ? 1 : 0);

    /* Start the main loop */
    xTaskCreatePinnedToCore([](void* arg) {
        Application* app = (Application*)arg;
        app->MainLoop();
        vTaskDelete(NULL);
    }, "main_loop", 4096 * 2, this, 4, &main_loop_task_handle_, 0);

    /* Wait for the network to be ready */
    board.StartNetwork();

    // 初始化协议
#ifdef CONFIG_CONNECTION_TYPE_WEBSOCKET
    protocol_ = std::make_unique<WebsocketProtocol>();
#else
    protocol_ = std::make_unique<MqttProtocol>();
#endif
    protocol_->OnNetworkError([this](const std::string& message) {
        SetDeviceState(kDeviceStateIdle);
        Alert(Lang::Strings::ERROR, message.c_str(), "sad", Lang::Sounds::P3_EXCLAMATION);
    });
    
    // 只有在音频编解码器可用时才设置音频相关回调
    if (codec) {
        protocol_->OnIncomingAudio([this](std::vector<uint8_t>&& data) {
            std::lock_guard<std::mutex> lock(mutex_);
            audio_decode_queue_.emplace_back(std::move(data));
        });
        protocol_->OnAudioChannelOpened([this, codec, &board]() {
            board.SetPowerSaveMode(false);
            if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
                ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                    protocol_->server_sample_rate(), codec->output_sample_rate());
            }
            SetDecodeSampleRate(protocol_->server_sample_rate(), protocol_->server_frame_duration());
            auto& thing_manager = iot::ThingManager::GetInstance();
            protocol_->SendIotDescriptors(thing_manager.GetDescriptorsJson());
            std::string states;
            if (thing_manager.GetStatesJson(states, false)) {
                protocol_->SendIotStates(states);
            }
        });
        protocol_->OnAudioChannelClosed([this, &board]() {
            board.SetPowerSaveMode(true);
            Schedule([this]() {
                SetDeviceState(kDeviceStateIdle);
            });
        });
    } else {
        // 音频编解码器不可用时，设置空的音频回调
        protocol_->OnIncomingAudio([](std::vector<uint8_t>&& data) {
            // 忽略音频数据
        });
        protocol_->OnAudioChannelOpened([this, &board]() {
            board.SetPowerSaveMode(false);
            auto& thing_manager = iot::ThingManager::GetInstance();
            protocol_->SendIotDescriptors(thing_manager.GetDescriptorsJson());
            std::string states;
            if (thing_manager.GetStatesJson(states, false)) {
                protocol_->SendIotStates(states);
            }
        });
        protocol_->OnAudioChannelClosed([this, &board]() {
            board.SetPowerSaveMode(true);
            Schedule([this]() {
                SetDeviceState(kDeviceStateIdle);
            });
        });
    }
    
    protocol_->Start();

    // 确保音频编解码器已经启动后再初始化音频处理器
    // 音频处理器初始化应该在硬件初始化完成后进行
    ESP_LOGI(TAG, "Waiting for audio codec to be ready...");
    vTaskDelay(pdMS_TO_TICKS(1000)); // 给硬件初始化一些时间

#if CONFIG_USE_AUDIO_PROCESSOR
    // 检查音频编解码器是否已启动
    if (codec && codec->input_enabled() && codec->output_enabled()) {
        ESP_LOGI(TAG, "Initializing audio processor...");
        audio_processor_.Initialize(codec, realtime_chat_enabled_);
        audio_processor_.OnOutput([this](std::vector<int16_t>&& data) {
            background_task_->Schedule([this, data = std::move(data)]() mutable {
                opus_encoder_->Encode(std::move(data), [this](std::vector<uint8_t>&& opus) {
                    Schedule([this, opus = std::move(opus)]() {
                        protocol_->SendAudio(opus);
                    });
                });
            });
        });
        audio_processor_.OnVadStateChange([this](bool speaking) {
            if (device_state_ == kDeviceStateListening) {
                Schedule([this, speaking]() {
                    if (speaking) {
                        voice_detected_ = true;
                    } else {
                        voice_detected_ = false;
                    }
                    auto led = Board::GetInstance().GetLed();
                    led->OnStateChanged();
                });
            }
        });
        ESP_LOGI(TAG, "Audio processor initialized successfully");
    } else {
        ESP_LOGW(TAG, "Audio codec not ready, skipping audio processor initialization");
    }
#endif

#if CONFIG_USE_WAKE_WORD_DETECT
    // wake_word_detect_.Initialize(codec);
    // wake_word_detect_.OnWakeWordDetected([this](const std::string& wake_word) { ... });
    // wake_word_detect_.StartDetection();
#endif

    SetDeviceState(kDeviceStateIdle);
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    // 硬件初始化应该在板级代码中完成，这里只保留应用逻辑
    // 板级代码会调用Application的回调函数来处理硬件事件
    
    // 显示初始状态
    Board::GetInstance().ShowDeviceState("idle");
    
    // 显示网络状态
    Board::GetInstance().ShowNetworkStatus(false);

#if 1
    while (true) {
        SystemInfo::PrintRealTimeStats(pdMS_TO_TICKS(1000));
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
#endif
}

void Application::OnClockTimer() {
    clock_ticks_++;

    // Print the debug info every 10 seconds
    if (clock_ticks_ % 10 == 0) {
        int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        int min_free_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "Free internal: %u minimal internal: %u", free_sram, min_free_sram);

        // If we have synchronized server time, set the status to clock "HH:MM" if the device is idle
        // if (ota_.HasServerTime()) {
        //     if (device_state_ == kDeviceStateIdle) {
        //         Schedule([this]() {
        //             // Set status to clock "HH:MM"
        //             time_t now = time(NULL);
        //             char time_str[64];
        //             strftime(time_str, sizeof(time_str), "%H:%M  ", localtime(&now));
        //             Board::GetInstance().GetDisplay()->SetStatus(time_str);
        //         });
        //     }
        // }
    }
}

// Add a async task to MainLoop
void Application::Schedule(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, SCHEDULE_EVENT);
}

// The Main Loop controls the chat state and websocket connection
// If other tasks need to access the websocket or chat state,
// they should use Schedule to call this function
void Application::MainLoop() {
    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, SCHEDULE_EVENT, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & SCHEDULE_EVENT) {
            std::unique_lock<std::mutex> lock(mutex_);
            std::list<std::function<void()>> tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }
    }
}

// The Audio Loop is used to input and output audio data
void Application::AudioLoop() {
    auto codec = Board::GetInstance().GetAudioCodec();
    
    // 如果音频编解码器不可用，直接返回
    if (!codec) {
        ESP_LOGW(TAG, "Audio codec not available, audio loop will not run");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    
    while (true) {
        OnAudioInput();
        if (codec->output_enabled()) {
            OnAudioOutput();
        }
    }
}

void Application::OnAudioOutput() {
    auto now = std::chrono::steady_clock::now();
    auto codec = Board::GetInstance().GetAudioCodec();
    const int max_silence_seconds = 10;

    std::unique_lock<std::mutex> lock(mutex_);
    if (audio_decode_queue_.empty()) {
        // Disable the output if there is no audio data for a long time
        if (device_state_ == kDeviceStateIdle) {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_output_time_).count();
            if (duration > max_silence_seconds) {
                // 通过Board接口控制音频输出，而不是直接控制硬件
                // codec->EnableOutput(false);
                Board::GetInstance().ShowDeviceState("idle");
            }
        }
        return;
    }

    if (device_state_ == kDeviceStateListening) {
        audio_decode_queue_.clear();
        return;
    }

    auto opus = std::move(audio_decode_queue_.front());
    audio_decode_queue_.pop_front();
    lock.unlock();

    background_task_->Schedule([this, codec, opus = std::move(opus)]() mutable {
        if (aborted_) {
            return;
        }

        // 只有在音频解码器可用时才处理音频
        if (!opus_decoder_) {
            ESP_LOGW(TAG, "Audio decoder not available, skipping audio output");
            return;
        }

        std::vector<int16_t> pcm;
        if (!opus_decoder_->Decode(std::move(opus), pcm)) {
            return;
        }
        // Resample if the sample rate is different
        if (opus_decoder_->sample_rate() != codec->output_sample_rate()) {
            int target_size = output_resampler_.GetOutputSamples(pcm.size());
            std::vector<int16_t> resampled(target_size);
            output_resampler_.Process(pcm.data(), pcm.size(), resampled.data());
            pcm = std::move(resampled);
        }
        codec->OutputData(pcm);
        last_output_time_ = std::chrono::steady_clock::now();
    });
}

void Application::OnAudioInput() {
    std::vector<int16_t> data;

#if CONFIG_USE_WAKE_WORD_DETECT
    if (wake_word_detect_.IsDetectionRunning()) {
        ReadAudio(data, 16000, wake_word_detect_.GetFeedSize());
        wake_word_detect_.Feed(data);
        return;
    }
#endif
#if CONFIG_USE_AUDIO_PROCESSOR
    if (audio_processor_.IsRunning()) {
        ReadAudio(data, 16000, audio_processor_.GetFeedSize());
        audio_processor_.Feed(data);
        return;
    }
#else
    if (device_state_ == kDeviceStateListening) {
        ReadAudio(data, 16000, 30 * 16000 / 1000);
        background_task_->Schedule([this, data = std::move(data)]() mutable {
            opus_encoder_->Encode(std::move(data), [this](std::vector<uint8_t>&& opus) {
                Schedule([this, opus = std::move(opus)]() {
                    protocol_->SendAudio(opus);
                });
            });
        });
        return;
    }
#endif
    vTaskDelay(pdMS_TO_TICKS(30));
}

void Application::ReadAudio(std::vector<int16_t>& data, int sample_rate, int samples) {
    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec->input_sample_rate() != sample_rate) {
        data.resize(samples * codec->input_sample_rate() / sample_rate);
        if (!codec->InputData(data)) {
            return;
        }
        if (codec->input_channels() == 2) {
            auto mic_channel = std::vector<int16_t>(data.size() / 2);
            auto reference_channel = std::vector<int16_t>(data.size() / 2);
            for (size_t i = 0, j = 0; i < mic_channel.size(); ++i, j += 2) {
                mic_channel[i] = data[j];
                reference_channel[i] = data[j + 1];
            }
            auto resampled_mic = std::vector<int16_t>(input_resampler_.GetOutputSamples(mic_channel.size()));
            auto resampled_reference = std::vector<int16_t>(reference_resampler_.GetOutputSamples(reference_channel.size()));
            input_resampler_.Process(mic_channel.data(), mic_channel.size(), resampled_mic.data());
            reference_resampler_.Process(reference_channel.data(), reference_channel.size(), resampled_reference.data());
            data.resize(resampled_mic.size() + resampled_reference.size());
            for (size_t i = 0, j = 0; i < resampled_mic.size(); ++i, j += 2) {
                data[j] = resampled_mic[i];
                data[j + 1] = resampled_reference[i];
            }
        } else {
            auto resampled = std::vector<int16_t>(input_resampler_.GetOutputSamples(data.size()));
            input_resampler_.Process(data.data(), data.size(), resampled.data());
            data = std::move(resampled);
        }
    } else {
        data.resize(samples);
        if (!codec->InputData(data)) {
            return;
        }
    }
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    protocol_->SendAbortSpeaking(reason);
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

void Application::SetDeviceState(DeviceState state) {
    if (device_state_ == state) {
        return;
    }
    
    clock_ticks_ = 0;
    auto previous_state = device_state_;
    device_state_ = state;
    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);
    
    // 通知Board接口设备状态变化
    OnDeviceStateChanged(state);
    
    // The state is changed, wait for all background tasks to finish
    background_task_->WaitForCompletion();

    auto& board = Board::GetInstance();
    // auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    switch (state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            // display->SetStatus(Lang::Strings::STANDBY);
            // display->SetEmotion("neutral");
#if CONFIG_USE_AUDIO_PROCESSOR
            audio_processor_.Stop();
#endif
#if CONFIG_USE_WAKE_WORD_DETECT
            // wake_word_detect_.StartDetection();
#endif
            break;
        case kDeviceStateConnecting:
            // display->SetStatus(Lang::Strings::CONNECTING);
            // display->SetEmotion("neutral");
            // display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            // display->SetStatus(Lang::Strings::LISTENING);
            // display->SetEmotion("neutral");

            // Update the IoT states before sending the start listening command
            UpdateIotStates();

            // Make sure the audio processor is running
#if CONFIG_USE_AUDIO_PROCESSOR
            if (!audio_processor_.IsRunning()) {
#else
            if (true) {
#endif
                // Send the start listening command
                protocol_->SendStartListening(listening_mode_);
                if (listening_mode_ == kListeningModeAutoStop && previous_state == kDeviceStateSpeaking) {
                    // FIXME: Wait for the speaker to empty the buffer
                    vTaskDelay(pdMS_TO_TICKS(120));
                }
                opus_encoder_->ResetState();
#if CONFIG_USE_WAKE_WORD_DETECT
                // wake_word_detect_.StopDetection();
#endif
#if CONFIG_USE_AUDIO_PROCESSOR
                audio_processor_.Start();
#endif
            }
            break;
        case kDeviceStateSpeaking:
            // display->SetStatus(Lang::Strings::SPEAKING);

            if (listening_mode_ != kListeningModeRealtime) {
#if CONFIG_USE_AUDIO_PROCESSOR
                audio_processor_.Stop();
#endif
#if CONFIG_USE_WAKE_WORD_DETECT
                // wake_word_detect_.StartDetection();
#endif
            }
            ResetDecoder();
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::ResetDecoder() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 只有在音频编解码器可用时才重置解码器
    if (opus_decoder_) {
        opus_decoder_->ResetState();
    }
    
    audio_decode_queue_.clear();
    last_output_time_ = std::chrono::steady_clock::now();
    
    // 通过Board接口控制音频输出，而不是直接控制硬件
    // auto codec = Board::GetInstance().GetAudioCodec();
    // codec->EnableOutput(true);
    Board::GetInstance().ShowDeviceState("speaking");
}

void Application::SetDecodeSampleRate(int sample_rate, int frame_duration) {
    // 只有在音频编解码器可用时才设置解码采样率
    if (!opus_decoder_) {
        ESP_LOGW(TAG, "Audio decoder not available, skipping sample rate setting");
        return;
    }
    
    if (opus_decoder_->sample_rate() == sample_rate && opus_decoder_->duration_ms() == frame_duration) {
        return;
    }

    opus_decoder_.reset();
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(sample_rate, 1, frame_duration);

    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec && opus_decoder_->sample_rate() != codec->output_sample_rate()) {
        ESP_LOGI(TAG, "Resampling audio from %d to %d", opus_decoder_->sample_rate(), codec->output_sample_rate());
        output_resampler_.Configure(opus_decoder_->sample_rate(), codec->output_sample_rate());
    }
}

void Application::UpdateIotStates() {
    auto& thing_manager = iot::ThingManager::GetInstance();
    std::string states;
    if (thing_manager.GetStatesJson(states, true)) {
        protocol_->SendIotStates(states);
    }
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    esp_restart();
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    if (device_state_ == kDeviceStateIdle) {
        ToggleChatState();
        Schedule([this, wake_word]() {
            if (protocol_) {
                protocol_->SendWakeWordDetected(wake_word); 
            }
        }); 
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

bool Application::CanEnterSleepMode() {
    if (device_state_ != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

// 这些全局变量应该移除，硬件访问应该通过Board接口

void Application::ShowVolumeIndicator(int volume) {
    // 通过Board接口实现音量指示
    Board::GetInstance().ShowVolumeIndicator(volume);
}

void Application::ShowBatteryLevel(int level) {
    // 通过Board接口实现电量指示
    Board::GetInstance().ShowBatteryLevel(level);
}

void Application::OnChargingStateChanged(bool charging) {
    // 通过Board接口实现充电状态指示
    Board::GetInstance().OnChargingStateChanged(charging);
}

void Application::OnNFCCardDetected(const std::string& uid) {
    ESP_LOGI(TAG, "NFC card detected: %s", uid.c_str());
    if (GetDeviceState() == kDeviceStateIdle) {
        ToggleChatState();
    }
}

void Application::OnNFCCardRemoved() {
    ESP_LOGI(TAG, "NFC card removed");
    if (GetDeviceState() == kDeviceStateListening || GetDeviceState() == kDeviceStateSpeaking) {
        StopListening();
    }
    // 主动断开 WiFi
    WifiStation::GetInstance().Stop();
    ESP_LOGI(TAG, "WiFi disconnected due to NFC removal");
}

// === 新增设备状态反馈方法实现 ===

void Application::OnDeviceStateChanged(DeviceState new_state) {
    ESP_LOGI(TAG, "Device state changed to: %s", STATE_STRINGS[new_state]);
    
    // 通过Board接口显示设备状态
    std::string state_str;
    switch (new_state) {
        case kDeviceStateIdle:
            state_str = "idle";
            break;
        case kDeviceStateListening:
            state_str = "listening";
            break;
        case kDeviceStateSpeaking:
            state_str = "speaking";
            break;
        case kDeviceStateConnecting:
            state_str = "connecting";
            break;
        case kDeviceStateUpgrading:
            state_str = "upgrading";
            break;
        case kDeviceStateFatalError:
            state_str = "error";
            break;
        default:
            state_str = "unknown";
            break;
    }
    
    Board::GetInstance().ShowDeviceState(state_str);
}

void Application::OnNetworkStatusChanged(bool connected) {
    ESP_LOGI(TAG, "Network status changed: %s", connected ? "Connected" : "Disconnected");
    // 移除对Board的调用，避免递归
    // Board::GetInstance().ShowNetworkStatus(connected);
}

void Application::OnButtonEvent(const std::string& button_name, bool is_long_press) {
    ESP_LOGI(TAG, "Button event: %s %s", button_name.c_str(), is_long_press ? "(long press)" : "(press)");
    
    if (is_long_press) {
        Board::GetInstance().OnButtonLongPressed(button_name);
    } else {
        Board::GetInstance().OnButtonPressed(button_name);
    }
}
