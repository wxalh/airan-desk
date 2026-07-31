#ifndef AIRAN_DATE_SIZE_FILE_SINK_H
#define AIRAN_DATE_SIZE_FILE_SINK_H

#include <spdlog/details/os.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <cstddef>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace spdlog::sinks
{

/* A daily-named file sink with a size limit for each file. */
template <typename Mutex>
class date_size_file_sink final : public base_sink<Mutex>
{
public:
    date_size_file_sink(filename_t base_filename,
                        std::size_t max_size,
                        std::size_t max_files = 0,
                        const file_event_handlers &event_handlers = {})
        : base_filename_(std::move(base_filename)),
          max_size_(max_size),
          max_files_(max_files),
          file_helper_(event_handlers)
    {
        if (base_filename_.empty())
            throw_spdlog_ex("date_size_file_sink: empty base filename");
        if (max_size_ == 0)
            throw_spdlog_ex("date_size_file_sink: max size must be greater than zero");

        current_filename_ = dailyFilename(spdlog::log_clock::now());
        file_helper_.open(current_filename_, false);
        current_size_ = file_helper_.size();
    }

    filename_t filename()
    {
        std::lock_guard<Mutex> lock(base_sink<Mutex>::mutex_);
        return file_helper_.filename();
    }

protected:
    void sink_it_(const details::log_msg &msg) override
    {
        const filename_t expectedFilename = dailyFilename(msg.time);
        if (expectedFilename != current_filename_)
        {
            file_helper_.open(expectedFilename, false);
            current_filename_ = expectedFilename;
            current_size_ = file_helper_.size();
        }

        memory_buf_t formatted;
        base_sink<Mutex>::formatter_->format(msg, formatted);
        if (current_size_ > 0 && current_size_ + formatted.size() > max_size_)
            rotateCurrentFile();

        file_helper_.write(formatted);
        current_size_ += formatted.size();
    }

    void flush_() override
    {
        file_helper_.flush();
    }

private:
    static filename_t dailyFilename(log_clock::time_point time,
                                    const filename_t &baseFilename)
    {
        const time_t timestamp = log_clock::to_time_t(time);
        const tm localTime = details::os::localtime(timestamp);
        return daily_filename_calculator::calc_filename(baseFilename, localTime);
    }

    filename_t dailyFilename(log_clock::time_point time) const
    {
        return dailyFilename(time, base_filename_);
    }

    filename_t rotatedFilename(std::size_t index) const
    {
        return rotating_file_sink<Mutex>::calc_filename(current_filename_, index);
    }

    void rotateCurrentFile()
    {
        file_helper_.flush();
        file_helper_.close();

        if (max_files_ == 0)
        {
            std::size_t index = 1;
            while (details::os::path_exists(rotatedFilename(index)))
                ++index;
            renameCurrentFile(rotatedFilename(index));
        }
        else
        {
            for (std::size_t index = max_files_; index > 1; --index)
            {
                const filename_t destination = rotatedFilename(index);
                details::os::remove_if_exists(destination);
                const filename_t source = rotatedFilename(index - 1);
                if (details::os::path_exists(source) && details::os::rename(source, destination) != 0)
                    throw_spdlog_ex("date_size_file_sink: failed rotating old file");
            }
            renameCurrentFile(rotatedFilename(1));
        }

        file_helper_.open(current_filename_, false);
        current_size_ = file_helper_.size();
    }

    void renameCurrentFile(const filename_t &destination)
    {
        details::os::remove_if_exists(destination);
        if (details::os::rename(current_filename_, destination) != 0)
            throw_spdlog_ex("date_size_file_sink: failed rotating current file");
    }

    filename_t base_filename_;
    std::size_t max_size_;
    std::size_t max_files_;
    filename_t current_filename_;
    std::size_t current_size_{0};
    details::file_helper file_helper_;
};

using date_size_file_sink_mt = date_size_file_sink<std::mutex>;
using date_size_file_sink_st = date_size_file_sink<details::null_mutex>;

} // namespace spdlog::sinks

#endif /* AIRAN_DATE_SIZE_FILE_SINK_H */
