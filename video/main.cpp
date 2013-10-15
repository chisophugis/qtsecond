#include "openglwindow.h"
#include <QApplication>
#include <QKeyEvent>
#include <QOpenGLShaderProgram>
#include <QScreen>
#include <Qt>
#include <QSize>
#include <QPixmap>
#include <QImage>
#include <QLabel>
#include <memory>
#include <vector>

// YUV4MPEG2 Stuff.

static const uchar *skipToAfterNewline(const uchar *P) {
  while (*P != '\n')
    ++P;
  return P + 1;
}

class YUV4MPEG2 {
public:
  YUV4MPEG2(const uchar *RawContents_, size_t RawSize_)
      : RawContents{RawContents_}, RawSize{RawSize_} {
    // FIXME: Actually parse the header. For now, assume:
    // - 352x288 4:2:0 YCbCr non-interleaved, one byte per sample.
    const uchar *FrameHeader = skipToAfterNewline(RawContents);
    while (FrameHeader < RawContents + RawSize) {
      const uchar *Y = skipToAfterNewline(FrameHeader);
      const uchar *Cb = skipLumaPlane(Y);
      const uchar *Cr = skipChromaPlane(Cb);
      Frames.push_back(Frame{Y, Cb, Cr});
      FrameHeader = skipChromaPlane(Cr);
    }
  }
  const uchar *skipLumaPlane(const uchar *P) { return P + Width * Height; }
  const uchar *skipChromaPlane(const uchar *P) {
    return P + (Width / 2) * (Height / 2);
  }
  struct Frame {
    const uchar *Y;
    const uchar *Cb;
    const uchar *Cr;
  };
  const uchar *RawContents;
  size_t RawSize;
  std::vector<Frame> Frames;
  int Width = 352;
  int Height = 288;
};

// GL Stuff.

const char VertexShaderSource[] = R"(
attribute highp vec4 posAttr;
attribute highp vec2 texCoordAttr;
varying highp vec2 texCoordVarying;
uniform highp mat4 matrix;
void main() {
  texCoordVarying = texCoordAttr;
  gl_Position = matrix * posAttr;
}
)";
const char FragmentShaderSource[] = R"(
varying highp vec2 texCoordVarying;
uniform sampler2D RGBTexture;
void main() {
  gl_FragColor = texture2D(RGBTexture, texCoordVarying.st);
}
)";

template <typename T>
T *typedNullptr() {
  return static_cast<T *>(nullptr);
}

template <typename MemberTy, typename StructTy>
GLvoid *offsetOfAsPtr(MemberTy StructTy::*MemberPtr) {
  return std::addressof(typedNullptr<StructTy>()->*MemberPtr);
}

struct Vertex {
  GLfloat XY[2];
  GLfloat ST[2];
};

// The default constructor of QOpenGLFunctions doesn't initialize with the
// current context. Instead, you have to pass a null pointer to the
// one-argument constructor (actually, this doesn't seem to be working, so
// hack around it by explicitly getting the current context). This class
// basically just avoids boilerplate in subclasses.
class OpenGLFunctions : protected QOpenGLFunctions {
public:
  OpenGLFunctions() : QOpenGLFunctions(QOpenGLContext::currentContext()) {}
};

class OpenGLFramebuffer : protected OpenGLFunctions {
  GLuint Name;

public:
  OpenGLFramebuffer() { glGenFramebuffers(1, &Name); }
  ~OpenGLFramebuffer() { glDeleteFramebuffers(1, &Name); }
  GLuint getName() { return Name; }
};

class OpenGLBuffer : protected OpenGLFunctions {
  GLuint Name;

public:
  OpenGLBuffer() { glGenBuffers(1, &Name); }
  ~OpenGLBuffer() { glDeleteBuffers(1, &Name); }
  GLuint getName() { return Name; }
};

// QOpenGLFunctions doesn't have any texture-related functions.
//
// The docs say "QOpenGLFunctions provides wrappers for all OpenGL/ES 2.0
// functions, except those like glDrawArrays(), glViewport(), and
// glBindTexture() that don't have portability issues."
// <http://qt-project.org/doc/qt-5.0/qtgui/qopenglfunctions.html>
class OpenGLTexture : protected OpenGLFunctions {
  GLuint Name;

public:
  OpenGLTexture() {
    glGenTextures(1, &Name);
    glBindTexture(GL_TEXTURE_2D, Name);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  }
  ~OpenGLTexture() { glDeleteTextures(1, &Name); }
  GLuint getName() { return Name; }
};

class YUVToRGBConverter : protected OpenGLFunctions {
  // We convert YUV->RGB into this framebuffer.
  OpenGLFramebuffer RGBConvertedFramebuffer;
  OpenGLTexture RGBTexture;

  // These are the inputs to the conversion process.
  OpenGLTexture LumaTexture;
  OpenGLTexture CbTexture;
  OpenGLTexture CrTexture;

  OpenGLBuffer ViewFillingSquareVertexBuffer;

  // TODO: I really need to find a better way to do this. Embedding the
  // shaders as string literals is just not doing it for me.
  QOpenGLShaderProgram Program;
  static const char VertexShaderSource[];
  static const char FragmentShaderSource[];

  YUVToRGBConverter(YUVToRGBConverter &) = delete;

public:
  YUVToRGBConverter() {
    Program.addShaderFromSourceCode(QOpenGLShader::Vertex, VertexShaderSource);
    Program.addShaderFromSourceCode(QOpenGLShader::Fragment,
                                    FragmentShaderSource);
    Program.link();

    // TODO: Investigate Vertex Array Objects, which encapsulate enabling
    // these vertex attributes and such.
    // Requires:
    // OpenGL ES 3.0, or OES_vertex_array_object
    // Desktop OpenGL 3.0 (ARB_vertex_array_object)

    // Notice that these texture coordinates have their Y-axis flipped
    // w.r.t. the vertex coordinates. That is because the image data itself
    // is arranged in memory starting at the top-left, while OpenGL
    // interprets textures in memory as starting at the bottom-left.
    Vertex Vertices[] = {               //
        {{-1.0f, -1.0f}, {0.0f, 1.0f}}, //
        {{-1.0f, 1.0f}, {0.0f, 0.0f}},  //
        {{1.0f, -1.0f}, {1.0f, 1.0f}},  //
        {{1.0f, 1.0f}, {1.0f, 0.0f}},   //
    };
    glBindBuffer(GL_ARRAY_BUFFER, ViewFillingSquareVertexBuffer.getName());
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vertices), &Vertices, GL_STATIC_DRAW);
  }
  void convertFrame(const YUV4MPEG2 &Y4M, int WhichFrame) {
    glBindFramebuffer(GL_FRAMEBUFFER, RGBConvertedFramebuffer.getName());

    glBindTexture(GL_TEXTURE_2D, RGBTexture.getName());
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, Y4M.Width, Y4M.Height, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           RGBTexture.getName(), 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      qDebug() << "Framebuffer not complete!";
    }
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // TODO: Abstract this.
    // For starters, see `od_img_plane` and `od_img` in the daala source.
    // Especially I like how it handles "decimation".
    const YUV4MPEG2::Frame &Frame = Y4M.Frames[WhichFrame];
    glBindTexture(GL_TEXTURE_2D, LumaTexture.getName());
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, Y4M.Width, Y4M.Height, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, Frame.Y);
    // XXX: Hardcoded division by 2 for 4:2:0. Breaks encapsulation of
    // YUV4MPEG2 class.
    glBindTexture(GL_TEXTURE_2D, CbTexture.getName());
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, Y4M.Width / 2, Y4M.Height / 2,
                 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, Frame.Cb);
    glBindTexture(GL_TEXTURE_2D, CrTexture.getName());
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, Y4M.Width / 2, Y4M.Height / 2,
                 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, Frame.Cr);

    // TODO: write an alternative version of this with the "nice" API
    // provided by QOpenGLShaderProgram, e.g.
    // setAttributeBuffer(const char * name, .....)
    // setUniformValue(const char * name, .....)

    Program.bind();

    GLuint YSamplerUniformLocation = Program.uniformLocation("YSampler");
    GLuint CbSamplerUniformLocation = Program.uniformLocation("CbSampler");
    GLuint CrSamplerUniformLocation = Program.uniformLocation("CrSampler");
    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D, LumaTexture.getName());
    glUniform1i(YSamplerUniformLocation, 0);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, CbTexture.getName());
    glUniform1i(CbSamplerUniformLocation, 1);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, CrTexture.getName());
    glUniform1i(CrSamplerUniformLocation, 2);

    glBindBuffer(GL_ARRAY_BUFFER, ViewFillingSquareVertexBuffer.getName());
    GLuint PositionAttributeLocation = Program.attributeLocation("Position");
    GLuint TexCoordAttributeLocation = Program.attributeLocation("TexCoord");
    glVertexAttribPointer(PositionAttributeLocation, 2, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex), offsetOfAsPtr(&Vertex::XY));
    glVertexAttribPointer(TexCoordAttributeLocation, 2, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex), offsetOfAsPtr(&Vertex::ST));

    glEnableVertexAttribArray(PositionAttributeLocation);
    glEnableVertexAttribArray(TexCoordAttributeLocation);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(TexCoordAttributeLocation);
    glDisableVertexAttribArray(PositionAttributeLocation);

    Program.release();
  }
  GLuint getRGBTextureName() { return RGBTexture.getName(); }
};

const char YUVToRGBConverter::VertexShaderSource[] = R"(
attribute highp vec4 Position;
attribute highp vec2 TexCoord;
varying highp vec2 vTexCoord;
void main() {
  vTexCoord = TexCoord;
  gl_Position = Position;
}
)";
const char YUVToRGBConverter::FragmentShaderSource[] = R"(
varying highp vec2 vTexCoord;
uniform sampler2D YSampler;
uniform sampler2D CbSampler;
uniform sampler2D CrSampler;
void main() {
  float Y = texture2D(YSampler, vTexCoord.st).r;
  float Cb = texture2D(CbSampler, vTexCoord.st).r;
  float Cr = texture2D(CrSampler, vTexCoord.st).r;
  // <http://www.equasys.de/colorconversion.html>
  // YUV4MPEG2 uses BT.601 with full-range [0,255] (i.e., no
  // headroom/footroom).
  // NOTE: The vectors passed in here are column-vectors, which are the
  // columns of the matrix, even though the physical arrangement of the
  // matrix entries in the source suggests that they are the rows.
  mat3 Conv = mat3(vec3(1.0, 1.0, 1.0),      //
                   vec3(0.0, -0.343, 1.765), //
                   vec3(1.4, -0.711, 0.0));
  gl_FragColor = vec4(Conv * vec3(Y, Cb - 0.5, Cr - 0.5), 1.0);
}
)";

class TriangleWindow : public OpenGLWindow {
public:
  TriangleWindow(YUV4MPEG2 &Y4M_) : Y4M(Y4M_) {}

  void keyPressEvent(QKeyEvent *E) override {
    int K = E->key();
    if (K == Qt::Key_K)
      ++TopVertexUpDown;
    else if (K == Qt::Key_J)
      --TopVertexUpDown;
    else if (K == Qt::Key_L)
      ++TopVertexLeftRight;
    else if (K == Qt::Key_H)
      --TopVertexLeftRight;
    else if (K == Qt::Key_Left)
      ++LeftRight;
    else if (K == Qt::Key_Right)
      --LeftRight;
    else if (K == Qt::Key_Up)
      ++UpDown;
    else if (K == Qt::Key_Down)
      --UpDown;
    render();
  }

  void initialize() override {
    // initializeGLFunctions();
    Program = new QOpenGLShaderProgram(this);
    Program->addShaderFromSourceCode(QOpenGLShader::Vertex, VertexShaderSource);
    Program->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                     FragmentShaderSource);
    Program->link();
  }
  GLuint createSimpleTexture() {
    GLuint Ret;
    glGenTextures(1, &Ret);
    glBindTexture(GL_TEXTURE_2D, Ret);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return Ret;
  }
  void render() override {
    Converter.convertFrame(Y4M, FrameNum % Y4M.Frames.size());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width(), height());

    glClearColor(0.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    Program->bind();

    // GLint MRS;
    // glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &MRS);
    // qDebug() << MRS; // 8192 on my computer.

    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D, Converter.getRGBTextureName());
    Program->setUniformValue("RGBTexture", 0);

    QMatrix4x4 M;
    // M.ortho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    // M.translate(0, UpDown, LeftRight);
    //
    M.perspective(60, static_cast<qreal>(width()) / height(), 0.1, 10.0);
    M.translate(0, 0, -2);
    // M.rotate(300.0 * FrameNum / screen()->refreshRate(), 0, 0, 1);

    // M.translate(0, UpDown, LeftRight);

    // This is the identity matrix:
    // M.ortho(-1.0, 1.0, -1.0, 1.0, 1.0, -1.0);
    // qDebug() << M;
    // qApp->quit();
    // The reason it is the identity is that OpenGL only rasterizes the
    // "projection space", which by definition is [-1,1]x[-1,1]x[-1,1], but
    // where the +z axis is coming towards you. The rasterization basically
    // sends rays orthogonal to the x-y plane, and starting at z=+1 ending
    // at z=-1 (everything else is clipped).
    // You can think of all of these matrix operations as just putting
    // things inside that box while transforming them so that when
    // rasterized they look how you want.
    // Perspective transformations effectively map a view frustrum onto the
    // projection space, which is a nonlinear transformation in 3D
    // Cartesian coordinates.
    // That's where the fourth "w" coordinate comes in; there is a fourth
    // coordinate which yields so-called "homogeneous coordinates", where
    // notionally <x,y,z,w> represents <x/w,y/w,z/w>. It turns out that
    // linearly interpolating in these homogeneous coordinates does what
    // you want if you make the w coordinate proportional to z (i.e.,
    // things are "scaled down" if they are farther away, since w becomes
    // larger).

    // M.translate(0, 0, -2);
    // M.rotate(100.0f * FrameNum / screen()->refreshRate(), 0, 1, 0);

    Program->setUniformValue("matrix", M);

    Vertex Vertices[] = {               //
        {{-1.0f, -1.0f}, {0.0f, 0.0f}}, // Bottom left.
        {{-1.0f, 1.0f}, {0.0f, 1.0f}},  // Top left.
        {{1.0f, -1.0f}, {1.0f, 0.0f}},  // Bottom right.
        {{1.0f, 1.0f}, {1.0f, 1.0f}},   // Top right.
    };

    GLuint VBOID;
    glGenBuffers(1, &VBOID);
    glBindBuffer(GL_ARRAY_BUFFER, VBOID);

    GLuint PosAttr = Program->attributeLocation("posAttr");
    GLuint TexCoordAttr = Program->attributeLocation("texCoordAttr");
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vertices), &Vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(PosAttr, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          offsetOfAsPtr(&Vertex::XY));
    glVertexAttribPointer(TexCoordAttr, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          offsetOfAsPtr(&Vertex::ST));

    glEnableVertexAttribArray(PosAttr);
    glEnableVertexAttribArray(TexCoordAttr);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(TexCoordAttr);
    glDisableVertexAttribArray(PosAttr);

    glDeleteBuffers(1, &VBOID);

    Program->release();
    ++FrameNum;
  }

private:
  int UpDown = 0;
  int LeftRight = 0;
  int TopVertexUpDown = 0;
  int TopVertexLeftRight = 0;

  YUVToRGBConverter Converter;
  QOpenGLShaderProgram *Program = nullptr;
  int FrameNum = 0;
  const YUV4MPEG2 &Y4M;
};

int main(int argc, char *argv[]) {
  QApplication A(argc, argv);

  static const char FOREMAN_CIF_PATH[] = "/home/sean/videos/foreman_cif.y4m";
  QFile F(FOREMAN_CIF_PATH);
  if (!F.open(QIODevice::ReadOnly))
    qFatal("Unable to open file: '%s'", FOREMAN_CIF_PATH);
  const uchar *RawFile = F.map(0, F.size());
  if (!RawFile)
    qFatal("Unable to map file: '%s'", FOREMAN_CIF_PATH);
  YUV4MPEG2 Y4M{RawFile, (size_t)F.size()};

  TriangleWindow W{Y4M};
  W.resize(Y4M.Width, Y4M.Height);
  W.show();
  W.setAnimating(true);

  return A.exec();
}
