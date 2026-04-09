#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <iostream>
#include <limits.h>
#include <string>
#include <unistd.h>
#include <vector>

#include "edgeInfer.h"
#include "image_drawing.h"
#include "image_utils.h"

namespace {

std::string dirname_of(const std::string& path) {
    const std::string::size_type pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return std::string(".");
    }
    if (pos == 0) {
        return std::string("/");
    }
    return path.substr(0, pos);
}

std::string get_install_root(const char* argv0) {
    char exe_path[PATH_MAX] = {0};
    const ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        exe_path[len] = '\0';
        return dirname_of(std::string(exe_path));
    }
    if (argv0 != nullptr) {
        return dirname_of(std::string(argv0));
    }
    return std::string(".");
}

void release_image(image_buffer_t* image) {
    if (image != nullptr && image->virt_addr != nullptr) {
        free(image->virt_addr);
        image->virt_addr = nullptr;
    }
}

bool clone_image(const image_buffer_t& source, image_buffer_t* target) {
    if (target == nullptr || source.virt_addr == nullptr || source.size <= 0) {
        return false;
    }

    *target = source;
    target->fd = 0;
    target->virt_addr = static_cast<unsigned char*>(malloc(source.size));
    if (target->virt_addr == nullptr) {
        target->size = 0;
        return false;
    }

    std::memcpy(target->virt_addr, source.virt_addr, source.size);
    return true;
}

int clamp_int(int value, int lower, int upper) {
    if (value < lower) {
        return lower;
    }
    if (value > upper) {
        return upper;
    }
    return value;
}

const unsigned int kBoxColors[] = {
    COLOR_GREEN,
    COLOR_RED,
    COLOR_BLUE,
    COLOR_YELLOW,
    COLOR_ORANGE,
    COLOR_WHITE,
};

bool draw_normalized_results(const image_buffer_t& input_image,
                             const std::vector<object_result>& results,
                             image_buffer_t* output_image) {
    if (!clone_image(input_image, output_image)) {
        return false;
    }

    const int image_width = input_image.width;
    const int image_height = input_image.height;
    const int thickness = 2;
    const int font_size = 12;
    const size_t color_count = sizeof(kBoxColors) / sizeof(kBoxColors[0]);

    for (const object_result& result : results) {
        const float box_width = result.box.w * static_cast<float>(image_width);
        const float box_height = result.box.h * static_cast<float>(image_height);
        const float left = result.box.x * static_cast<float>(image_width) - box_width / 2.0f;
        const float top = result.box.y * static_cast<float>(image_height) - box_height / 2.0f;

        int draw_x = static_cast<int>(std::lround(left));
        int draw_y = static_cast<int>(std::lround(top));
        int draw_w = static_cast<int>(std::lround(box_width));
        int draw_h = static_cast<int>(std::lround(box_height));

        draw_x = clamp_int(draw_x, 0, image_width - 1);
        draw_y = clamp_int(draw_y, 0, image_height - 1);
        draw_w = clamp_int(draw_w, 1, image_width - draw_x);
        draw_h = clamp_int(draw_h, 1, image_height - draw_y);

        const unsigned int color = kBoxColors[static_cast<size_t>(result.class_id >= 0 ? result.class_id : 0) % color_count];
        draw_rectangle(output_image, draw_x, draw_y, draw_w, draw_h, color, thickness);

        char label[128] = {0};
        if (!result.class_name.empty()) {
            std::snprintf(label, sizeof(label), "%s %.2f", result.class_name.c_str(), result.prop);
        } else {
            std::snprintf(label, sizeof(label), "%d %.2f", result.class_id, result.prop);
        }
        draw_text(output_image, label, draw_x, draw_y > font_size ? draw_y - font_size : 0, color, font_size);
    }

    return true;
}

std::string append_suffix_to_path(const std::string& path, const std::string& suffix) {
    const std::string::size_type slash_pos = path.find_last_of('/');
    const std::string::size_type dot_pos = path.find_last_of('.');
    if (dot_pos == std::string::npos || (slash_pos != std::string::npos && dot_pos < slash_pos)) {
        return path + suffix;
    }
    return path.substr(0, dot_pos) + suffix + path.substr(dot_pos);
}

void print_results(const std::string& title, const std::vector<object_result>& results) {
    std::cout << title << std::endl;
    std::cout << "detect count: " << results.size() << std::endl;
    for (size_t index = 0; index < results.size(); ++index) {
        const object_result& result = results[index];
        std::cout << "[" << index << "] class_id=" << result.class_id
                  << ", class_name=" << result.class_name
                  << ", score=" << result.prop
                  << ", box=(x=" << result.box.x
                  << ", y=" << result.box.y
                  << ", w=" << result.box.w
                  << ", h=" << result.box.h << ")"
                  << std::endl;
    }
}

}

int main(int argc, char** argv) {
    const std::string install_root = get_install_root(argc > 0 ? argv[0] : nullptr);
    const std::string default_config = install_root + "/weights/hardhatWare.json";
    const std::string default_image = install_root + "/images/000411.jpg";
    const std::string default_output = install_root + "/hardhat_detect_demo.jpg";
    const std::string default_output_three_args = append_suffix_to_path(default_output, "_three_args");

    const std::string config_path = argc > 1 ? argv[1] : default_config;
    const std::string image_path = argc > 2 ? argv[2] : default_image;
    const std::string output_path = argc > 3 ? argv[3] : default_output;
    const std::string output_path_three_args = argc > 4 ? argv[4] : default_output_three_args;

    std::cout << "config: " << config_path << std::endl;
    std::cout << "image : " << image_path << std::endl;
    std::cout << "output (4 args): " << output_path << std::endl;
    std::cout << "output (3 args): " << output_path_three_args << std::endl;

    EdgeInfer infer;
    if (infer.init(config_path) != RET_SUCCESS) {
        std::cerr << "failed to init EdgeInfer with config: " << config_path << std::endl;
        return 1;
    }

    image_buffer_t input_image;
    image_buffer_t output_image;
    std::memset(&input_image, 0, sizeof(input_image));
    std::memset(&output_image, 0, sizeof(output_image));

    if (read_image(image_path.c_str(), &input_image) != RET_SUCCESS) {
        std::cerr << "failed to read image: " << image_path << std::endl;
        return 1;
    }

    std::vector<object_result> results;
    filter_list_t filters;
    std::memset(&filters, 0, sizeof(filters));
    filters.confidence_threshold = 0.0f;

    if (infer.infer(input_image, filters, results, output_image) != RET_SUCCESS) {
        std::cerr << "infer failed" << std::endl;
        release_image(&input_image);
        release_image(&output_image);
        return 1;
    }

    print_results("4-parameter infer results:", results);

    if (write_image(output_path.c_str(), &output_image) != RET_SUCCESS) {
        std::cerr << "failed to write result image: " << output_path << std::endl;
        release_image(&input_image);
        release_image(&output_image);
        return 1;
    }

    std::cout << "4-parameter result image saved to: " << output_path << std::endl;

    std::vector<object_result> results_three_args;
    image_buffer_t output_image_three_args;
    std::memset(&output_image_three_args, 0, sizeof(output_image_three_args));

    if (infer.infer(input_image, filters, results_three_args) != RET_SUCCESS) {
        std::cerr << "3-parameter infer failed" << std::endl;
        release_image(&input_image);
        release_image(&output_image);
        release_image(&output_image_three_args);
        return 1;
    }

    print_results("3-parameter infer results:", results_three_args);

    if (!draw_normalized_results(input_image, results_three_args, &output_image_three_args)) {
        std::cerr << "failed to draw 3-parameter infer results" << std::endl;
        release_image(&input_image);
        release_image(&output_image);
        release_image(&output_image_three_args);
        return 1;
    }

    if (write_image(output_path_three_args.c_str(), &output_image_three_args) != RET_SUCCESS) {
        std::cerr << "failed to write 3-parameter result image: " << output_path_three_args << std::endl;
        release_image(&input_image);
        release_image(&output_image);
        release_image(&output_image_three_args);
        return 1;
    }

    std::cout << "3-parameter result image saved to: " << output_path_three_args << std::endl;

    release_image(&input_image);
    release_image(&output_image);
    release_image(&output_image_three_args);
    return 0;
}