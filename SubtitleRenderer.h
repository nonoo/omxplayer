#pragma once

// Author: Torarin Hals Bakke (2012)

// Boost Software License - Version 1.0 - August 17th, 2003

// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:

// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include <EGL/egl.h>
#include <VG/openvg.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_STROKER_H
#include <boost/config.hpp>
#include <vector>
#include <unordered_map>
#include <string>

class TagTracker {
public:
  TagTracker() : italic_(), state_(), closing_() {};

  void put(char32_t cp) {
    if (state_ == '>')
      state_ = 0;

    switch (cp) {
      case '<':
        state_ = '<';
        closing_ = false;
        break;
      case '/':
        if (state_ == '<')
          closing_ = true;
        break;
      case 'i':
        if (state_)
          state_ = 'i';
        break;
      case '>':
        if (state_) {
          if (state_ == 'i')
            italic_ = !closing_;
          state_ = '>';
        }
        break;
    }
  }

  bool italic() {
    return italic_;
  }

  bool in_tag() {
    return state_;
  }

private:
  bool italic_;
  char state_;
  bool closing_;
};

typedef struct {
  int buffer_width;
  int buffer_height;
  int buffer_y;
  int buffer_x;
  int line_height;
  int box_offset;
  int box_h_padding;
  int margin_left;
  int margin_bottom;
  int title_line_height;
  int title_line_padding;
  int title_box_offset;
  int title_box_h_padding;
} SubtitleConfig;

class SubtitleRenderer {
public:
  SubtitleRenderer(const SubtitleRenderer&) = delete;
  SubtitleRenderer& operator=(const SubtitleRenderer&) = delete;
  SubtitleRenderer(int display, int layer,
                   const std::string& font_path,
                   const std::string& italic_font_path,
                   const std::string& title_font_path,
                   float font_size,
                   float title_font_size,
                   float margin_left,
                   float margin_bottom,
                   bool centered,
                   bool title_centered,
                   unsigned int white_level,
                   unsigned int box_opacity,
                   unsigned int lines);
  ~SubtitleRenderer() BOOST_NOEXCEPT;

  void prepare(const std::vector<std::string>& text_lines) BOOST_NOEXCEPT;
  void prepare_title(const std::string& line) BOOST_NOEXCEPT;

  void unprepare() BOOST_NOEXCEPT {
    prepared_ = false;
  }

  void show_next() BOOST_NOEXCEPT {
    if (title_prepared_)
      draw_title(1);
    if (prepared_) {
      // puts("Expensive show_next!");
      draw(!title_prepared_);
    }
    swap_buffers();
  }

  void hide() BOOST_NOEXCEPT {
    if (title_prepared_)
      draw_title(1);
    else
      clear();

    swap_buffers();

    if (title_prepared_)
      draw_title(1);
    if (prepared_)
      draw(!title_prepared_);
  }

  void set_rect(int width, int height, int x, int y) BOOST_NOEXCEPT;

private:
  struct InternalChar {
    InternalChar() = default;
    InternalChar(char32_t codepoint, bool italic) {
      val = codepoint | (static_cast<char32_t>(italic) << 31);
    }
    
    bool operator ==(const InternalChar& other) const {
      return val == other.val;
    }

    char32_t codepoint() const { return val & 0x7FFFFFFF; }
    bool italic() const { return val >> 31; }

    char32_t val;
  };

  struct InternalCharHash {
    size_t operator()(InternalChar ch) const noexcept {
      return static_cast<size_t>(ch.val);
    }
  };

  struct InternalGlyph {
    int advance;
  };

  static void draw_text(VGFont font,
                        const std::vector<InternalChar>& text,
                        int x, int y,
                        unsigned int lightness);

  void destroy();
  void initialize_fonts(const std::string& font_name,
                        const std::string& italic_font_path,
                        const std::string& title_font_path);
  void destroy_fonts();
  void initialize_vg();
  void destroy_vg();
  void initialize_window(int display, int layer);
  void destroy_window();
  void clear() BOOST_NOEXCEPT;
  void draw_title(bool clear_needed) BOOST_NOEXCEPT;
  void draw(bool clear_needed) BOOST_NOEXCEPT;
  void swap_buffers() BOOST_NOEXCEPT;
  void prepare_glyphs(const std::vector<InternalChar>& text, bool title);
  void load_glyph(InternalChar ch, bool title);
  int get_text_width(const std::vector<InternalChar>& text, bool title);
  std::vector<InternalChar> get_internal_chars(const std::string& str,
                                               TagTracker& tag_tracker);

  bool prepared_;
  bool title_prepared_;
  DISPMANX_ELEMENT_HANDLE_T dispman_element_;
  DISPMANX_DISPLAY_HANDLE_T dispman_display_;
  EGLDisplay display_;
  EGLContext context_;
  EGLSurface surface_;
  VGFont vg_font_;
  VGFont vg_font_border_;
  VGFont vg_font_title_;
  VGFont vg_font_title_border_;
  FT_Library ft_library_;
  FT_Face ft_face_;
  FT_Face ft_face_italic_;
  FT_Face ft_face_title_;
  FT_Stroker ft_stroker_;
  std::unordered_map<InternalChar,InternalGlyph, InternalCharHash> glyphs_;
  std::unordered_map<InternalChar,InternalGlyph, InternalCharHash> glyphs_title_;
  std::vector<std::vector<InternalChar>> internal_lines_;
  std::vector<std::pair<int,int>> line_positions_;
  std::vector<int> line_widths_;
  std::vector<InternalChar> internal_title_line_;
  std::pair<int,int> title_line_position_;
  int title_line_width_;
  bool centered_;
  bool title_centered_;
  unsigned int white_level_;
  unsigned int box_opacity_;
  uint32_t screen_width_;
  uint32_t screen_height_;
  float font_size_;
  float title_font_size_;
  SubtitleConfig config_fullscreen_;
  SubtitleConfig config_;
};
