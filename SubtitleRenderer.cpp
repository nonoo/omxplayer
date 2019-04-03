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

#include "SubtitleRenderer.h"
#include "Unicode.h"
#include "utils/ScopeExit.h"
#include "utils/Enforce.h"
#include "utils/Clamp.h"

#include <bcm_host.h>
#include <VG/vgu.h>
#include <cassert>
#include <algorithm>

#include "bcm_host.h"

class BoxRenderer {
  VGPath path_;
  VGPaint paint_;

public:
  BoxRenderer(unsigned int opacity) {
    path_ = vgCreatePath(VG_PATH_FORMAT_STANDARD, VG_PATH_DATATYPE_F,
                        1.0, 0.0,
                        0, 0,
                        VG_PATH_CAPABILITY_ALL);
    assert(path_);

    paint_ = vgCreatePaint();
    assert(paint_);

    vgSetColor(paint_, opacity);
    assert(!vgGetError());
  }

  ~BoxRenderer() {
    vgDestroyPath(path_);
    assert(!vgGetError());
    vgDestroyPaint(paint_);
    assert(!vgGetError());
  }

  BoxRenderer(const BoxRenderer&) = delete;
  BoxRenderer& operator=(const BoxRenderer&) = delete;

  void clear() {
    vgClearPath(path_, VG_PATH_CAPABILITY_ALL);
    assert(!vgGetError());
  }

  void push(int x, int y, int width, int height) {
    assert(width >= 0);
    assert(height >= 0);

    vguRect(path_, x, y, width, height);
    assert(!vgGetError());
  };

  void render() {
    vgSetPaint(paint_, VG_FILL_PATH);
    assert(!vgGetError());

    vgDrawPath(path_, VG_FILL_PATH);
    assert(!vgGetError());
  }
};

void SubtitleRenderer::load_glyph(InternalChar ch, bool title) {
  VGfloat escapement[2]{};

  auto load_glyph_internal =
  [&](FT_Face ft_face, VGFont vg_font, bool border) {
    try {
      auto glyph_index = FT_Get_Char_Index(ft_face, ch.codepoint());
      ENFORCE(!FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_NO_HINTING));

      FT_Glyph glyph;
      ENFORCE(!FT_Get_Glyph(ft_face->glyph, &glyph));
      SCOPE_EXIT {FT_Done_Glyph(glyph);};

      if (border)
        ENFORCE(!FT_Glyph_StrokeBorder(&glyph, ft_stroker_, 0, 1));

      ENFORCE(!FT_Glyph_To_Bitmap(&glyph, FT_RENDER_MODE_NORMAL, NULL, 1));
      FT_BitmapGlyph bit_glyph = (FT_BitmapGlyph) glyph;
      FT_Bitmap& bitmap = bit_glyph->bitmap;

      VGImage image{};
      VGfloat glyph_origin[2]{};

      if (bitmap.width > 0 && bitmap.rows > 0) {
        constexpr VGfloat blur_stddev = 0.52;
        const int padding = static_cast<int>(3*blur_stddev + 0.5);
        const int image_width = bitmap.width + padding*2;
        const int image_height = bitmap.rows + padding*2;

        image = vgCreateImage(VG_A_8, image_width, image_height,
                              VG_IMAGE_QUALITY_NONANTIALIASED);
        assert(image);
        
        if (bitmap.pitch > 0) {
          vgImageSubData(image,
                         bitmap.buffer + bitmap.pitch*(bitmap.rows-1),
                         -bitmap.pitch,
                         VG_A_8,
                         padding,
                         padding,
                         bitmap.width,
                         bitmap.rows);
          assert(!vgGetError());
        } else {
          vgImageSubData(image,
                         bitmap.buffer,
                         bitmap.pitch,
                         VG_A_8,
                         padding,
                         padding,
                         bitmap.width,
                         bitmap.rows);
          assert(!vgGetError());
        }

        auto softened_image = vgCreateImage(VG_A_8,
                                            image_width,
                                            image_height,
                                            VG_IMAGE_QUALITY_NONANTIALIASED);
        assert(softened_image);

        // Even out hard and soft edges
        vgGaussianBlur(softened_image, image, blur_stddev, blur_stddev, VG_TILE_FILL);
        assert(!vgGetError());

        vgDestroyImage(image);
        assert(!vgGetError());

        image = softened_image;

        glyph_origin[0] = static_cast<VGfloat>(padding - bit_glyph->left);
        glyph_origin[1] = static_cast<VGfloat>(padding + static_cast<int>(bitmap.rows) - bit_glyph->top - 1);
      }

      escapement[0] = static_cast<VGfloat>((ft_face->glyph->advance.x + 32) / 64);
      escapement[1] = 0;

      vgSetGlyphToImage(vg_font, ch.val, image, glyph_origin, escapement);
      assert(!vgGetError());

      if (image) {
        vgDestroyImage(image);
        assert(!vgGetError());
      }
    } catch(...) {
      escapement[0] = 0;
      escapement[1] = 0;
      vgSetGlyphToImage(vg_font, ch.val, VG_INVALID_HANDLE, escapement, escapement);
      assert(!vgGetError());
    }
  };

  if (title) {
    load_glyph_internal(ft_face_title_, vg_font_title_, false);
    glyphs_title_[ch].advance = escapement[0];
    load_glyph_internal(ft_face_title_, vg_font_title_border_, true);
    return;
  }

  if (!ch.italic()) {
    load_glyph_internal(ft_face_, vg_font_, false);
    glyphs_[ch].advance = escapement[0];
    load_glyph_internal(ft_face_, vg_font_border_, true);
  } else {
    load_glyph_internal(ft_face_italic_, vg_font_, false);
    glyphs_[ch].advance = escapement[0];
    load_glyph_internal(ft_face_italic_, vg_font_border_, true);
  }
}

int SubtitleRenderer::get_text_width(const std::vector<InternalChar>& text, bool title) {
  int width = 0;
  for (auto c = text.begin(); c != text.end(); ++c) {
    if (title)
        width += glyphs_title_.at(*c).advance;
    else
      width += glyphs_.at(*c).advance;
  }
  return width;
}

std::vector<SubtitleRenderer::InternalChar> SubtitleRenderer::
get_internal_chars(const std::string& str, TagTracker& tag_tracker) {
  std::vector<InternalChar> internal_chars;
  auto c_str = str.c_str();
  for (size_t i = 0, len = str.length(); i < len;) {
    try {
      auto cp = decodeUtf8(c_str, len, i);
      tag_tracker.put(cp);
      if (!tag_tracker.in_tag())
        internal_chars.push_back(InternalChar(cp, tag_tracker.italic()));
    } catch (...) {
      ++i; // Keep going
    }
  }
  return internal_chars;
}

void SubtitleRenderer::
prepare_glyphs(const std::vector<InternalChar>& text, bool title) {
  for (auto c = text.begin(); c != text.end(); ++c) {
    if (title) {
      if (glyphs_title_.find(*c) == glyphs_title_.end())
        load_glyph(*c, title);
    } else {
      if (glyphs_.find(*c) == glyphs_.end())
        load_glyph(*c, title);
    }
  }
}

void SubtitleRenderer::
draw_text(VGFont font,
          const std::vector<SubtitleRenderer::InternalChar>& text,
          int x, int y,
          unsigned int lightness) {
  VGPaint paint = vgCreatePaint();
  assert(paint);

  vgSetColor(paint, (lightness<<8) | (lightness<<16) | (lightness<<24) | 0xFF);
  assert(!vgGetError());

  vgSetPaint(paint, VG_FILL_PATH);
  assert(!vgGetError());

  vgDestroyPaint(paint);
  assert(!vgGetError());

  vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_MULTIPLY);
  assert(!vgGetError());

  VGfloat pos[] = {static_cast<VGfloat>(x), static_cast<VGfloat>(y)};

  vgSetfv(VG_GLYPH_ORIGIN, 2, pos);
  assert(!vgGetError());

  for (auto c = text.begin(); c != text.end(); ++c) {
    vgDrawGlyph(font, c->val, VG_FILL_PATH, VG_FALSE);
    assert(!vgGetError());
  }
}


SubtitleRenderer::~SubtitleRenderer() BOOST_NOEXCEPT {
  destroy();
}

SubtitleRenderer::
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
                 unsigned int lines)
: show_subtitle_(),
  title_prepared_(),
  time_prepared_(),
  dispman_element_(),
  dispman_display_(),
  display_(),
  context_(),
  surface_(),
  vg_font_(),
  vg_font_border_(),
  vg_font_title_(),
  vg_font_title_border_(),
  ft_library_(),
  ft_face_(),
  ft_face_italic_(),
  ft_face_title_(),
  ft_stroker_(),
  prepared_lines_(),
  prepared_lines_active_(),
  centered_(centered),
  title_centered_(title_centered),
  white_level_(white_level),
  box_opacity_(box_opacity),
  font_size_(font_size),
  title_font_size_(title_font_size)
{
  try {

    ENFORCE(graphics_get_display_size(display, &screen_width_, &screen_height_) >= 0);
    initialize_fonts(font_path, italic_font_path, title_font_path);

    int abs_margin_bottom =
      static_cast<int>(margin_bottom * screen_height_ + 0.5f) - config_.box_offset;

    int buffer_padding = (config_.line_height+2)/4;
    int buffer_bottom = clamp(abs_margin_bottom + config_.box_offset - buffer_padding,
                              0, (int) screen_height_-1);
    int buffer_top = clamp(buffer_bottom + config_.title_line_height + config_.title_line_padding +
                           config_.line_height * (int) lines + buffer_padding*2,
                           0, (int) screen_height_-1);

    config_.buffer_x = 0;
    config_.buffer_y = screen_height_ - buffer_top - 1;
    config_.buffer_width = screen_width_;
    config_.buffer_height = buffer_top - buffer_bottom + 1;
    config_.margin_left = static_cast<int>(margin_left * screen_width_ + 0.5f);

    config_.margin_bottom = abs_margin_bottom - buffer_bottom;
    config_fullscreen_ = config_; // save full-screen config for scaling reference.

    initialize_window(display, layer);

    initialize_vg();
  } catch (...) {
    destroy();
    throw;
  }
}

void SubtitleRenderer::destroy() {
  destroy_vg();
  destroy_window();
  destroy_fonts();
}

void SubtitleRenderer::
initialize_fonts(const std::string& font_path,
                 const std::string& italic_font_path,
                 const std::string& title_font_path) {
  ENFORCE(!FT_Init_FreeType(&ft_library_));
  ENFORCE2(!FT_New_Face(ft_library_, font_path.c_str(), 0, &ft_face_),
           "Unable to open font");
  ENFORCE2(!FT_New_Face(ft_library_, italic_font_path.c_str(), 0, &ft_face_italic_),
           "Unable to open italic font");
  ENFORCE2(!FT_New_Face(ft_library_, title_font_path.c_str(), 0, &ft_face_title_),
           "Unable to open title font");

  uint32_t font_size = font_size_*screen_height_;
  uint32_t title_font_size = title_font_size_*screen_height_;
  ENFORCE(!FT_Set_Pixel_Sizes(ft_face_, 0, font_size));
  ENFORCE(!FT_Set_Pixel_Sizes(ft_face_italic_, 0, font_size));
  ENFORCE(!FT_Set_Pixel_Sizes(ft_face_title_, 0, title_font_size));

  auto get_bbox = [this](char32_t cp, FT_Face& font) {
    auto glyph_index = FT_Get_Char_Index(font, cp);
    ENFORCE(!FT_Load_Glyph(font, glyph_index, FT_LOAD_NO_HINTING));
    FT_Glyph glyph;
    ENFORCE(!FT_Get_Glyph(font->glyph, &glyph));
    SCOPE_EXIT {FT_Done_Glyph(glyph);};
    FT_BBox bbox;
    FT_Glyph_Get_CBox(glyph, FT_GLYPH_BBOX_PIXELS, &bbox);
    return bbox;
  };

  constexpr float padding_factor = 0.05f;
  int y_min = get_bbox('g', ft_face_).yMin;
  int y_max = get_bbox('M', ft_face_).yMax;
  y_max += -y_min*0.7f;
  config_.line_height = y_max - y_min;
  int v_padding = config_.line_height*padding_factor + 0.5f;
  config_.line_height += v_padding*2;
  config_.box_offset = y_min-v_padding;
  config_.box_h_padding = config_.line_height/5.0f + 0.5f;

  y_min = get_bbox('g', ft_face_title_).yMin;
  y_max = get_bbox('M', ft_face_title_).yMax;
  y_max += -y_min*0.7f;
  config_.title_line_height = y_max - y_min;
  v_padding = config_.title_line_height*padding_factor + 0.5f;
  config_.title_line_height += v_padding*2;
  config_.title_line_padding = config_.line_height * 0.5f + 0.5f;
  config_.title_box_offset = y_min-v_padding;
  config_.title_box_h_padding = config_.title_line_height/5.0f + 0.5f;

  constexpr float border_thickness = 0.044f;
  ENFORCE(!FT_Stroker_New(ft_library_, &ft_stroker_));
  FT_Stroker_Set(ft_stroker_,
                 config_.line_height*border_thickness*64.0f,
                 FT_STROKER_LINECAP_ROUND,
                 FT_STROKER_LINEJOIN_ROUND,
                 0);
}

void SubtitleRenderer::destroy_fonts() {
  if (ft_library_) {
    auto error = FT_Done_FreeType(ft_library_);
    assert(!error);
    ft_library_ = {};
    ft_face_ = {};
    ft_face_italic_ = {};
    ft_face_title_ = {};
    ft_stroker_ = {};
  }
} 

void SubtitleRenderer::initialize_window(int display, int layer) {
  VC_RECT_T dst_rect;
  dst_rect.x = config_.buffer_x;
  dst_rect.y = config_.buffer_y;
  dst_rect.width = config_.buffer_width;
  dst_rect.height = config_.buffer_height;

  VC_RECT_T src_rect;
  src_rect.x = 0;
  src_rect.y = 0;
  src_rect.width = dst_rect.width << 16;
  src_rect.height = dst_rect.height << 16;        

  dispman_display_ = vc_dispmanx_display_open(display);
  ENFORCE(dispman_display_);

  {
    auto dispman_update = vc_dispmanx_update_start(0);
    ENFORCE(dispman_update);
    SCOPE_EXIT {
      ENFORCE(!vc_dispmanx_update_submit_sync(dispman_update));
    };

    dispman_element_ =
        vc_dispmanx_element_add(dispman_update,
                                dispman_display_,
                                layer,
                                &dst_rect,
                                0 /*src*/,
                                &src_rect,
                                DISPMANX_PROTECTION_NONE,
                                0 /*alpha*/,
                                0 /*clamp*/,
                                DISPMANX_STEREOSCOPIC_MONO);
    ENFORCE(dispman_element_);
  }
}

void SubtitleRenderer::destroy_window() {  
  if (dispman_element_) {
    auto dispman_update = vc_dispmanx_update_start(0);
    assert(dispman_update);

    if (dispman_update) {
      auto error = vc_dispmanx_element_remove(dispman_update, dispman_element_);
      assert(!error);

      error = vc_dispmanx_update_submit_sync(dispman_update);
      assert(!error);
    }

    dispman_element_ = {};
  }

  if (dispman_display_) {
    auto error = vc_dispmanx_display_close(dispman_display_);
    assert(!error);

    dispman_display_ = {};
  }
}

void SubtitleRenderer::initialize_vg() {
  // get an EGL display connection
  display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  ENFORCE(display_);

  // initialize the EGL display connection
  ENFORCE(eglInitialize(display_, NULL, NULL));

  // get an appropriate EGL frame buffer configuration
  static const EGLint attribute_list[] = {
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_NONE
  };
  EGLConfig config{};
  EGLint num_config{};

  ENFORCE(eglChooseConfig(display_, attribute_list, &config, 1, &num_config));
  ENFORCE(num_config);

  ENFORCE(eglBindAPI(EGL_OPENVG_API));

  static EGL_DISPMANX_WINDOW_T nativewindow;
  nativewindow.element = dispman_element_;
  nativewindow.width = config_.buffer_width;
  nativewindow.height = config_.buffer_height;
     
  surface_ = eglCreateWindowSurface(display_, config, &nativewindow, NULL);
  ENFORCE(surface_);

  // create an EGL rendering context
  context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, NULL);
  ENFORCE(context_);

  auto result = eglMakeCurrent(display_, surface_, surface_, context_);
  assert(result);

  vgSeti(VG_FILTER_FORMAT_LINEAR, VG_TRUE);
  assert(!vgGetError());

  vgSeti(VG_IMAGE_QUALITY, VG_IMAGE_QUALITY_NONANTIALIASED);
  assert(!vgGetError());

  auto create_vg_font = [](VGFont& font) {
    font = vgCreateFont(64);
    ENFORCE(font);
  };

  create_vg_font(vg_font_);
  create_vg_font(vg_font_border_);

  create_vg_font(vg_font_title_);
  create_vg_font(vg_font_title_border_);

  // VGfloat color[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
  // vgSetfv(VG_CLEAR_COLOR, 4, color);
}

void SubtitleRenderer::destroy_vg() {
  if (display_) {
    auto result =
      eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    assert(result);

    result = eglTerminate(display_);
    assert(result);

    context_ = {};
    surface_ = {};
    display_ = {};
  }
}

void SubtitleRenderer::
prepare(const std::vector<std::string>& text_lines) BOOST_NOEXCEPT {
  const int n_lines = text_lines.size();
  TagTracker tag_tracker;
  PreparedSubtitleLines& lines = prepared_lines_[!prepared_lines_active_];

  lines.internal_lines_.resize(n_lines);
  lines.line_widths_.resize(n_lines);
  lines.line_positions_.resize(n_lines);

  int title_line_height = config_.title_line_height;
  int title_line_padding = config_.title_line_padding;

  if (!title_prepared_) {
    title_line_height = 0;
    title_line_padding = 0;
  }

  for (int i = 0; i < n_lines; ++i) {
    lines.internal_lines_[i] = get_internal_chars(text_lines[i], tag_tracker);
    prepare_glyphs(lines.internal_lines_[i], false);
    lines.line_widths_[i] = get_text_width(lines.internal_lines_[i], false);
    lines.line_positions_[i].second = config_.margin_bottom +
      title_line_height + title_line_padding +
      (n_lines-i-1)*config_.line_height;
    if (centered_)
      lines.line_positions_[i].first = config_.buffer_width/2 - lines.line_widths_[i]/2;
    else
      lines.line_positions_[i].first = config_.margin_left;
  }

  lines.prepared_ = true;
}

void SubtitleRenderer::
prepare_time(const std::string& line) BOOST_NOEXCEPT {
  TagTracker tag_tracker;

  if (line == "") {
    time_prepared_ = false;
    return;
  }

  internal_time_ = get_internal_chars(line, tag_tracker);
  prepare_glyphs(internal_time_, true);
  time_width_ = get_text_width(internal_time_, true);
  time_position_.second = config_.margin_bottom;
  time_position_.first = config_.buffer_width - time_width_ - config_.margin_left;

  time_prepared_ = true;
}

void SubtitleRenderer::
prepare_title(const std::string& line) BOOST_NOEXCEPT {
  TagTracker tag_tracker;

  if (line == "") {
    title_prepared_ = false;
    return;
  }

  internal_title_line_ = get_internal_chars(line, tag_tracker);
  prepare_glyphs(internal_title_line_, true);
  title_line_width_ = get_text_width(internal_title_line_, true);
  title_line_position_.second = config_.margin_bottom;
  if (title_centered_)
    title_line_position_.first = config_.buffer_width/2 - title_line_width_/2;
  else
    title_line_position_.first = config_.margin_left;

  title_prepared_ = true;
}

void SubtitleRenderer::clear() BOOST_NOEXCEPT {
  vgClear(0, 0, screen_width_, screen_height_);
  assert(!vgGetError());
}

void SubtitleRenderer::draw_time(bool clear_needed) BOOST_NOEXCEPT {
  if (clear_needed)
    clear();

  // time graybox
  {
    BoxRenderer box_renderer(box_opacity_);
    box_renderer.push(time_position_.first - config_.box_h_padding,
                    time_position_.second + config_.title_box_offset,
                    time_width_ + config_.title_box_h_padding*2,
                    config_.title_line_height);
    box_renderer.render();
  }

  // time background
  draw_text(vg_font_title_border_,
            internal_time_,
            time_position_.first, time_position_.second,
            0);

  // time foreground
  draw_text(vg_font_title_,
            internal_time_,
            time_position_.first, time_position_.second,
            white_level_);
}

void SubtitleRenderer::draw_title(bool clear_needed) BOOST_NOEXCEPT {
  if (clear_needed)
    clear();

  // title line graybox
  {
    BoxRenderer box_renderer(box_opacity_);
    box_renderer.push(title_line_position_.first - config_.box_h_padding,
                    title_line_position_.second + config_.title_box_offset,
                    title_line_width_ + config_.title_box_h_padding*2,
                    config_.title_line_height);
    box_renderer.render();
  }

  // title line background
  draw_text(vg_font_title_border_,
            internal_title_line_,
            title_line_position_.first, title_line_position_.second,
            0);

  // title line foreground
  draw_text(vg_font_title_,
            internal_title_line_,
            title_line_position_.first, title_line_position_.second,
            white_level_);
}

void SubtitleRenderer::draw(bool clear_needed) BOOST_NOEXCEPT {
  PreparedSubtitleLines& lines = prepared_lines_[prepared_lines_active_];

  if (clear_needed)
    clear();

  const auto n_lines = lines.internal_lines_.size();

  // font graybox
  {
    BoxRenderer box_renderer(box_opacity_);
    for (size_t i = 0; i < n_lines; ++i) {
      box_renderer.push(lines.line_positions_[i].first - config_.box_h_padding,
                        lines.line_positions_[i].second + config_.box_offset,
						lines.line_widths_[i] + config_.box_h_padding*2,
                        config_.line_height);
    }
    box_renderer.render();
  }

  //font background
  for (size_t i = 0; i < n_lines; ++i) {
    draw_text(vg_font_border_,
              lines.internal_lines_[i],
              lines.line_positions_[i].first, lines.line_positions_[i].second,
              0);
  }

  //font foreground
  for (size_t i = 0; i < n_lines; ++i) {
    draw_text(vg_font_,
              lines.internal_lines_[i],
              lines.line_positions_[i].first, lines.line_positions_[i].second,
              white_level_);
  }
}

void SubtitleRenderer::swap_buffers() BOOST_NOEXCEPT {
  EGLBoolean result = eglSwapBuffers(display_, surface_);
  assert(result);
}

void SubtitleRenderer::set_rect(int x1, int y1, int x2, int y2) BOOST_NOEXCEPT
{
    uint32_t width = x2-x1;
    uint32_t height = y2-y1;
    float height_mod = (float) height / screen_height_;
    float width_mod = (float) width / screen_width_;
    config_.buffer_x = x1;
    config_.buffer_y = y2 - (screen_height_ - config_fullscreen_.buffer_y) * height_mod + 0.5f;
    config_.buffer_width = width;
    config_.buffer_height = config_fullscreen_.buffer_height * height_mod + 0.5f;
    config_.line_height = config_fullscreen_.line_height * height_mod + 0.5f;
    config_.box_offset = config_fullscreen_.box_offset * height_mod + 0.5f;
    config_.box_h_padding = config_fullscreen_.box_h_padding * height_mod + 0.5f;
    config_.margin_left = config_fullscreen_.margin_left * width_mod + 0.5f;
    config_.margin_bottom = config_fullscreen_.margin_bottom * height_mod + 0.5f;
    config_.title_line_height = config_fullscreen_.title_line_height * height_mod + 0.5f;
    config_.title_line_padding = config_fullscreen_.title_line_padding * height_mod + 0.5f;
    config_.title_box_offset = config_fullscreen_.title_box_offset * height_mod + 0.5f;
    config_.title_box_h_padding = config_fullscreen_.title_box_h_padding * height_mod + 0.5f;

    // resize dispmanx element
    ENFORCE(dispman_element_);
    VC_RECT_T dst_rect;
    vc_dispmanx_rect_set(&dst_rect, config_.buffer_x, config_.buffer_y, config_.buffer_width, config_.buffer_height);
    VC_RECT_T src_rect;
    vc_dispmanx_rect_set(&src_rect, x1, y1, config_.buffer_width<<16, config_.buffer_height<<16);
    DISPMANX_UPDATE_HANDLE_T dispman_update;
    dispman_update = vc_dispmanx_update_start(0);
    ENFORCE(dispman_update);
    uint32_t change_flag = 1<<2 | 1<<3; // change only dst_rect and src_rect
    ENFORCE(!vc_dispmanx_element_change_attributes(dispman_update, dispman_element_, change_flag, 0, 0,
                                                   &dst_rect, &src_rect, 0, (DISPMANX_TRANSFORM_T) 0));
    ENFORCE(!vc_dispmanx_update_submit_sync(dispman_update));

    // resize font
    glyphs_.clear(); // clear cached glyphs
    glyphs_title_.clear();
    float font_size = height*font_size_;
    float title_font_size = height*title_font_size_;
    ENFORCE(!FT_Set_Pixel_Sizes(ft_face_, 0, font_size));
    ENFORCE(!FT_Set_Pixel_Sizes(ft_face_italic_, 0, font_size));
    ENFORCE(!FT_Set_Pixel_Sizes(ft_face_title_, 0, title_font_size));
}
