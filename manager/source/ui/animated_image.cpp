/*
 * Copyright (c) 2026 NedcloarBR
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <ui/animated_image.hpp>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <functional>
#include <vector>
#include <gif_lib.h>
#include <webp/demux.h>

namespace ui {

    namespace {

        using Clock = std::chrono::steady_clock;

        std::atomic<bool> g_in_focus = true;

        /* 30 frames a second at most; GIF delays under 20 ms mean 100 ms, as browsers play them. */
        constexpr u32 MinFrameMs = 33;

        /* Decodes frames one after another, back to the first after the last, as RGBA over the whole canvas. */
        class Decoder {
            public:
                virtual ~Decoder() = default;
                virtual s32 Width() const = 0;
                virtual s32 Height() const = 0;
                /* The next frame into `rgba` (Width x Height x 4) and how long it shows; false on a broken file. */
                virtual bool Next(u8 *rgba, u32 *delay_ms) = 0;
                /* Frames in one loop, known once the first loop was read (0 before). */
                virtual u32 FramesPerLoop() const = 0;
        };

        class GifDecoder final : public Decoder {
            private:
                const std::vector<u8> &data;
                size_t offset = 0;
                GifFileType *gif = nullptr;
                s32 width = 0, height = 0;
                std::vector<u8> canvas, saved;
                std::vector<GifPixelType> line;
                GraphicsControlBlock pending = { DISPOSAL_UNSPECIFIED, false, 0, NO_TRANSPARENT_COLOR };
                /* What the last frame asks to be done with its area before the next one is drawn. */
                int last_disposal = DISPOSAL_UNSPECIFIED;
                s32 last_x = 0, last_y = 0, last_w = 0, last_h = 0;
                u32 frames_this_loop = 0, frames_per_loop = 0;

                static int Read(GifFileType *gif, GifByteType *out, int size) {
                    auto *self = static_cast<GifDecoder *>(gif->UserData);
                    const size_t count = std::min<size_t>(static_cast<size_t>(size), self->data.size() - self->offset);
                    std::memcpy(out, self->data.data() + self->offset, count);
                    self->offset += count;
                    return static_cast<int>(count);
                }

                bool Reopen() {
                    if (this->gif != nullptr) {
                        DGifCloseFile(this->gif, nullptr);
                    }
                    this->offset = 0;
                    int error = 0;
                    this->gif = DGifOpen(this, Read, &error);
                    if (this->gif == nullptr) {
                        return false;
                    }
                    this->width = this->gif->SWidth;
                    this->height = this->gif->SHeight;
                    this->canvas.assign(static_cast<size_t>(this->width) * this->height * 4, 0);
                    this->last_disposal = DISPOSAL_UNSPECIFIED;
                    this->pending = { DISPOSAL_UNSPECIFIED, false, 0, NO_TRANSPARENT_COLOR };
                    return this->width > 0 && this->height > 0;
                }

                void ClearArea(s32 x, s32 y, s32 w, s32 h) {
                    for (s32 row = y; row < y + h; ++row) {
                        std::memset(&this->canvas[(static_cast<size_t>(row) * this->width + x) * 4], 0, static_cast<size_t>(w) * 4);
                    }
                }

                bool DrawImage() {
                    if (DGifGetImageDesc(this->gif) == GIF_ERROR) {
                        return false;
                    }
                    const GifImageDesc &desc = this->gif->Image;
                    const ColorMapObject *map = desc.ColorMap != nullptr ? desc.ColorMap : this->gif->SColorMap;
                    if (map == nullptr || desc.Width <= 0 || desc.Height <= 0) {
                        return false;
                    }

                    /* What the previous frame left behind. */
                    if (this->last_disposal == DISPOSE_BACKGROUND) {
                        this->ClearArea(this->last_x, this->last_y, this->last_w, this->last_h);
                    } else if (this->last_disposal == DISPOSE_PREVIOUS && this->saved.size() == this->canvas.size()) {
                        this->canvas = this->saved;
                    }
                    if (this->pending.DisposalMode == DISPOSE_PREVIOUS) {
                        this->saved = this->canvas;
                    }

                    /* Clipped to the canvas; rows outside it are still read. */
                    const s32 x0 = std::clamp(desc.Left, 0, this->width), y0 = std::clamp(desc.Top, 0, this->height);
                    const s32 x1 = std::clamp(desc.Left + desc.Width, 0, this->width), y1 = std::clamp(desc.Top + desc.Height, 0, this->height);
                    this->line.resize(static_cast<size_t>(desc.Width));
                    const auto draw_row = [&](s32 row) {
                        const s32 y = desc.Top + row;
                        if (y < y0 || y >= y1) {
                            return;
                        }
                        u8 *out = &this->canvas[(static_cast<size_t>(y) * this->width + x0) * 4];
                        for (s32 x = x0; x < x1; ++x, out += 4) {
                            const GifPixelType index = this->line[static_cast<size_t>(x - desc.Left)];
                            if (index == this->pending.TransparentColor || index >= map->ColorCount) {
                                continue;
                            }
                            const GifColorType &c = map->Colors[index];
                            out[0] = c.Red;
                            out[1] = c.Green;
                            out[2] = c.Blue;
                            out[3] = 0xFF;
                        }
                    };
                    if (desc.Interlace) {
                        constexpr s32 Start[] = { 0, 4, 2, 1 }, Step[] = { 8, 8, 4, 2 };
                        for (int pass = 0; pass < 4; ++pass) {
                            for (s32 row = Start[pass]; row < desc.Height; row += Step[pass]) {
                                if (DGifGetLine(this->gif, this->line.data(), desc.Width) == GIF_ERROR) {
                                    return false;
                                }
                                draw_row(row);
                            }
                        }
                    } else {
                        for (s32 row = 0; row < desc.Height; ++row) {
                            if (DGifGetLine(this->gif, this->line.data(), desc.Width) == GIF_ERROR) {
                                return false;
                            }
                            draw_row(row);
                        }
                    }

                    this->last_disposal = this->pending.DisposalMode;
                    this->last_x = x0;
                    this->last_y = y0;
                    this->last_w = x1 - x0;
                    this->last_h = y1 - y0;
                    return true;
                }

            public:
                explicit GifDecoder(const std::vector<u8> &data) : data(data) {}

                ~GifDecoder() override {
                    if (this->gif != nullptr) {
                        DGifCloseFile(this->gif, nullptr);
                    }
                }

                bool Open() {
                    return this->Reopen();
                }

                s32 Width() const override { return this->width; }
                s32 Height() const override { return this->height; }
                u32 FramesPerLoop() const override { return this->frames_per_loop; }

                bool Next(u8 *rgba, u32 *delay_ms) override {
                    /* A loop with no frame in it ends the animation; two restarts in a row mean the same. */
                    for (int restarts = 0; restarts < 2;) {
                        GifRecordType type = UNDEFINED_RECORD_TYPE;
                        if (DGifGetRecordType(this->gif, &type) == GIF_ERROR) {
                            return false;
                        }
                        if (type == EXTENSION_RECORD_TYPE) {
                            int code = 0;
                            GifByteType *extension = nullptr;
                            if (DGifGetExtension(this->gif, &code, &extension) == GIF_ERROR) {
                                return false;
                            }
                            if (code == GRAPHICS_EXT_FUNC_CODE && extension != nullptr && extension[0] >= 4) {
                                DGifExtensionToGCB(extension[0], extension + 1, &this->pending);
                            }
                            while (extension != nullptr) {
                                if (DGifGetExtensionNext(this->gif, &extension) == GIF_ERROR) {
                                    return false;
                                }
                            }
                        } else if (type == IMAGE_DESC_RECORD_TYPE) {
                            if (!this->DrawImage()) {
                                return false;
                            }
                            const u32 delay = static_cast<u32>(this->pending.DelayTime) * 10;
                            *delay_ms = delay < 20 ? 100 : delay;
                            this->pending = { DISPOSAL_UNSPECIFIED, false, 0, NO_TRANSPARENT_COLOR };
                            ++this->frames_this_loop;
                            std::memcpy(rgba, this->canvas.data(), this->canvas.size());
                            return true;
                        } else if (type == TERMINATE_RECORD_TYPE) {
                            if (this->frames_per_loop == 0) {
                                this->frames_per_loop = this->frames_this_loop;
                            }
                            if (this->frames_this_loop == 0) {
                                ++restarts;
                            }
                            this->frames_this_loop = 0;
                            if (!this->Reopen()) {
                                return false;
                            }
                        }
                    }
                    return false;
                }
        };

        class WebpDecoder final : public Decoder {
            private:
                WebPData data;
                WebPAnimDecoder *decoder = nullptr;
                WebPAnimInfo info = {};
                int last_timestamp = 0;

            public:
                explicit WebpDecoder(const std::vector<u8> &bytes) : data{ bytes.data(), bytes.size() } {}

                ~WebpDecoder() override {
                    if (this->decoder != nullptr) {
                        WebPAnimDecoderDelete(this->decoder);
                    }
                }

                bool Open() {
                    WebPAnimDecoderOptions options;
                    if (!WebPAnimDecoderOptionsInit(&options)) {
                        return false;
                    }
                    options.color_mode = MODE_RGBA;
                    options.use_threads = 0;
                    this->decoder = WebPAnimDecoderNew(&this->data, &options);
                    return this->decoder != nullptr && WebPAnimDecoderGetInfo(this->decoder, &this->info) && this->info.frame_count > 0;
                }

                s32 Width() const override { return static_cast<s32>(this->info.canvas_width); }
                s32 Height() const override { return static_cast<s32>(this->info.canvas_height); }
                u32 FramesPerLoop() const override { return this->info.frame_count; }

                bool Next(u8 *rgba, u32 *delay_ms) override {
                    if (!WebPAnimDecoderHasMoreFrames(this->decoder)) {
                        WebPAnimDecoderReset(this->decoder);
                        this->last_timestamp = 0;
                    }
                    u8 *frame = nullptr;
                    int timestamp = 0;
                    if (!WebPAnimDecoderGetNext(this->decoder, &frame, &timestamp) || frame == nullptr) {
                        return false;
                    }
                    *delay_ms = static_cast<u32>(std::max(0, timestamp - this->last_timestamp));
                    this->last_timestamp = timestamp;
                    std::memcpy(rgba, frame, static_cast<size_t>(this->Width()) * this->Height() * 4);
                    return true;
                }
        };

        bool EndsWith(const std::string &text, const char *suffix) {
            const size_t length = std::strlen(suffix);
            if (text.size() < length) {
                return false;
            }
            for (size_t i = 0; i < length; ++i) {
                if (std::tolower(static_cast<unsigned char>(text[text.size() - length + i])) != suffix[i]) {
                    return false;
                }
            }
            return true;
        }

        bool ReadFile(const std::string &path, std::vector<u8> *out) {
            FILE *file = std::fopen(path.c_str(), "rb");
            if (file == nullptr) {
                return false;
            }
            std::fseek(file, 0, SEEK_END);
            const long size = std::ftell(file);
            std::rewind(file);
            bool ok = size > 0 && size <= AnimatedImage::MaxFileSize;
            if (ok) {
                out->resize(static_cast<size_t>(size));
                ok = std::fread(out->data(), 1, out->size(), file) == out->size();
            }
            std::fclose(file);
            return ok;
        }

        /* The decoder for the file's kind, opened; null when it is neither or cannot be read. */
        std::unique_ptr<Decoder> OpenDecoder(const std::string &path, const std::vector<u8> &data) {
            if (EndsWith(path, ".gif")) {
                auto gif = std::make_unique<GifDecoder>(data);
                return gif->Open() ? std::move(gif) : nullptr;
            }
            if (EndsWith(path, ".webp")) {
                auto webp = std::make_unique<WebpDecoder>(data);
                return webp->Open() ? std::move(webp) : nullptr;
            }
            return nullptr;
        }

        SDL_Texture *MakeTexture(s32 width, s32 height, SDL_TextureAccess access) {
            SDL_Texture *texture = SDL_CreateTexture(pu::ui::render::GetMainRenderer(), SDL_PIXELFORMAT_ABGR8888, access, width, height);
            if (texture != nullptr) {
                SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
            }
            return texture;
        }

    }

    struct AnimatedImage::Impl {
        std::vector<u8> file;
        std::unique_ptr<Decoder> decoder;
        s32 width = 0, height = 0;
        /* The decoder thread fills `next`; the frame due is copied to `ready` for the render thread. */
        std::vector<u8> next, ready;
        bool has_ready = false;
        bool stop = false;
        std::mutex mutex;
        std::condition_variable wake;
        Thread thread = {};
        bool has_thread = false;
        std::function<void()> body;
        SDL_Texture *texture = nullptr;

        static void Entry(void *arg) {
            static_cast<Impl *>(arg)->body();
        }

        /* Decoding a 720p frame takes tens of ms: on the UI's core at its priority, Horizon would not switch back to
         * the UI until it is done (same-priority threads only take turns when one waits). So it runs on another core,
         * below the UI's priority, and only falls back to the UI's core (still below it) when that core is not ours. */
        bool StartThread(std::function<void()> run) {
            this->body = std::move(run);
            constexpr size_t Stack = 256 * 1024;
            constexpr int Priority = 0x3B;
            for (const int core : { 1, 2, -2 }) {
                if (R_SUCCEEDED(threadCreate(&this->thread, Entry, this, nullptr, Stack, Priority, core))) {
                    if (R_SUCCEEDED(threadStart(&this->thread))) {
                        this->has_thread = true;
                        return true;
                    }
                    threadClose(&this->thread);
                }
            }
            return false;
        }

        /* Shows each decoded frame when the one before it has been up for its delay; `due` is the first one's time. */
        void Run(Clock::time_point due) {
            while (true) {
                {
                    std::unique_lock lock(this->mutex);
                    while (!this->stop && !g_in_focus) {
                        this->wake.wait_for(lock, std::chrono::milliseconds(100));
                    }
                    if (this->stop) {
                        return;
                    }
                }
                u32 delay = 0;
                if (!this->decoder->Next(this->next.data(), &delay)) {
                    return;
                }
                std::unique_lock lock(this->mutex);
                if (this->wake.wait_until(lock, due, [this]() { return this->stop; })) {
                    return;
                }
                std::memcpy(this->ready.data(), this->next.data(), this->ready.size());
                this->has_ready = true;
                due = std::max(due, Clock::now() - std::chrono::milliseconds(MinFrameMs)) + std::chrono::milliseconds(std::max(MinFrameMs, delay));
            }
        }
    };

    std::unique_ptr<AnimatedImage> AnimatedImage::Open(const std::string &path) {
        auto impl = std::make_unique<Impl>();
        if (!ReadFile(path, &impl->file)) {
            return nullptr;
        }
        impl->decoder = OpenDecoder(path, impl->file);
        if (impl->decoder == nullptr) {
            return nullptr;
        }
        impl->width = impl->decoder->Width();
        impl->height = impl->decoder->Height();
        if (impl->width <= 0 || impl->height <= 0 || impl->width > MaxWidth || impl->height > MaxHeight) {
            return nullptr;
        }
        const size_t bytes = static_cast<size_t>(impl->width) * impl->height * 4;
        impl->next.resize(bytes);
        impl->ready.resize(bytes);

        /* The first frame shows at once; one frame in all is a still image, left to the image loader. */
        u32 delay = 0;
        if (!impl->decoder->Next(impl->ready.data(), &delay)) {
            return nullptr;
        }
        u32 second = 0;
        if (!impl->decoder->Next(impl->next.data(), &second) || impl->decoder->FramesPerLoop() == 1) {
            return nullptr;
        }
        impl->has_ready = true;
        impl->texture = MakeTexture(impl->width, impl->height, SDL_TEXTUREACCESS_STREAMING);
        if (impl->texture == nullptr) {
            return nullptr;
        }
        /* The frame already decoded into `next` is the second; the thread decodes from the third on. */
        Impl *raw = impl.get();
        auto image = std::unique_ptr<AnimatedImage>(new AnimatedImage(std::move(impl)));
        const bool started = raw->StartThread([raw, delay, second]() {
            {
                std::unique_lock lock(raw->mutex);
                if (raw->wake.wait_for(lock, std::chrono::milliseconds(std::max(MinFrameMs, delay)), [raw]() { return raw->stop; })) {
                    return;
                }
                std::memcpy(raw->ready.data(), raw->next.data(), raw->ready.size());
                raw->has_ready = true;
            }
            raw->Run(Clock::now() + std::chrono::milliseconds(std::max(MinFrameMs, second)));
        });
        return started ? std::move(image) : nullptr;
    }

    SDL_Texture *AnimatedImage::LoadFirstFrame(const std::string &path) {
        std::vector<u8> file;
        if (!ReadFile(path, &file)) {
            return nullptr;
        }
        const auto decoder = OpenDecoder(path, file);
        if (decoder == nullptr || decoder->Width() <= 0 || decoder->Height() <= 0 || decoder->Width() > MaxWidth || decoder->Height() > MaxHeight) {
            return nullptr;
        }
        std::vector<u8> rgba(static_cast<size_t>(decoder->Width()) * decoder->Height() * 4);
        u32 delay = 0;
        if (!decoder->Next(rgba.data(), &delay)) {
            return nullptr;
        }
        SDL_Texture *texture = MakeTexture(decoder->Width(), decoder->Height(), SDL_TEXTUREACCESS_STATIC);
        if (texture != nullptr) {
            SDL_UpdateTexture(texture, nullptr, rgba.data(), decoder->Width() * 4);
        }
        return texture;
    }

    bool AnimatedImage::IsAnimated(const std::string &path) {
        std::vector<u8> file;
        if (!ReadFile(path, &file)) {
            return false;
        }
        const auto decoder = OpenDecoder(path, file);
        if (decoder == nullptr || decoder->Width() <= 0 || decoder->Height() <= 0 || decoder->Width() > MaxWidth || decoder->Height() > MaxHeight) {
            return false;
        }
        std::vector<u8> rgba(static_cast<size_t>(decoder->Width()) * decoder->Height() * 4);
        u32 delay = 0;
        return decoder->Next(rgba.data(), &delay) && decoder->Next(rgba.data(), &delay) && decoder->FramesPerLoop() != 1;
    }

    void AnimatedImage::SetInFocus(bool in_focus) {
        g_in_focus = in_focus;
    }

    AnimatedImage::AnimatedImage(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {}

    AnimatedImage::~AnimatedImage() {
        {
            std::lock_guard lock(this->impl->mutex);
            this->impl->stop = true;
        }
        this->impl->wake.notify_all();
        if (this->impl->has_thread) {
            threadWaitForExit(&this->impl->thread);
            threadClose(&this->impl->thread);
        }
        if (this->impl->texture != nullptr) {
            SDL_DestroyTexture(this->impl->texture);
        }
    }

    SDL_Texture *AnimatedImage::Frame() {
        std::lock_guard lock(this->impl->mutex);
        if (this->impl->has_ready) {
            SDL_UpdateTexture(this->impl->texture, nullptr, this->impl->ready.data(), this->impl->width * 4);
            this->impl->has_ready = false;
        }
        return this->impl->texture;
    }

}
