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
uniform sampler2D YSampler;
uniform sampler2D CbSampler;
uniform sampler2D CrSampler;
void main() {
  float Y = texture2D(YSampler, texCoordVarying.st).r;
  float Cb = texture2D(CbSampler, texCoordVarying.st).r;
  float Cr = texture2D(CrSampler, texCoordVarying.st).r;
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
    PosAttr = Program->attributeLocation("posAttr");
    TexCoordAttr = Program->attributeLocation("texCoordAttr");
    MatrixUniform = Program->uniformLocation("matrix");
    YSamplerUniformLocation = Program->uniformLocation("YSampler");
    CbSamplerUniformLocation = Program->uniformLocation("CbSampler");
    CrSamplerUniformLocation = Program->uniformLocation("CrSampler");

    LumaTexture = createSimpleTexture();
    CbTexture = createSimpleTexture();
    CrTexture = createSimpleTexture();
  }
  GLuint createSimpleTexture() {
    GLuint Ret;
    glGenTextures(1, &Ret);
    glBindTexture(GL_TEXTURE_2D, Ret);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return Ret;
  }
  void render() override {
    glViewport(0, 0, width(), height());

    glClearColor(0.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    Program->bind();

    // TODO: Abstract this.

    const YUV4MPEG2::Frame &Frame = Y4M.Frames[FrameNum % Y4M.Frames.size()];
    glBindTexture(GL_TEXTURE_2D, LumaTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, Y4M.Width, Y4M.Height, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, Frame.Y);
    // XXX: Hardcoded division by 2 for 4:2:0. Breaks encapsulation of
    // YUV4MPEG2 class.
    glBindTexture(GL_TEXTURE_2D, CbTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, Y4M.Width / 2, Y4M.Height / 2,
                 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, Frame.Cb);
    glBindTexture(GL_TEXTURE_2D, CrTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, Y4M.Width / 2, Y4M.Height / 2,
                 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, Frame.Cr);

    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D, LumaTexture);
    glUniform1i(YSamplerUniformLocation, 0);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, CbTexture);
    glUniform1i(CbSamplerUniformLocation, 1);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, CrTexture);
    glUniform1i(CrSamplerUniformLocation, 2);

    QMatrix4x4 M;
    // M.ortho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    // M.perspective(60, static_cast<qreal>(width()) / height(), 0.1, 100.0);
    M.translate(0, UpDown, LeftRight);

    // M.translate(0, 0, -2);
    // M.rotate(100.0f * FrameNum / screen()->refreshRate(), 0, 1, 0);

    Program->setUniformValue(MatrixUniform, M);

    Vertex Vertices[] = {               //
        {{-1.0f, -1.0f}, {0.0f, 1.0f}}, //
        {{-1.0f, 1.0f}, {0.0f, 0.0f}},  //
        {{1.0f, -1.0f}, {1.0f, 1.0f}},  //
        {{1.0f, 1.0f}, {1.0f, 0.0f}},   //
    };

    GLuint VBOID;
    glGenBuffers(1, &VBOID);
    glBindBuffer(GL_ARRAY_BUFFER, VBOID);

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
  ~TriangleWindow() {
    glDeleteTextures(1, &LumaTexture);
    glDeleteTextures(1, &CbTexture);
    glDeleteTextures(1, &CrTexture);
  }

private:
  int UpDown = 0;
  int LeftRight = 0;
  int TopVertexUpDown = 0;
  int TopVertexLeftRight = 0;
  GLuint PosAttr;
  GLuint TexCoordAttr;
  GLuint MatrixUniform;
  GLuint YSamplerUniformLocation;
  GLuint CbSamplerUniformLocation;
  GLuint CrSamplerUniformLocation;
  GLuint LumaTexture;
  GLuint CbTexture;
  GLuint CrTexture;

  QOpenGLShaderProgram *Program = nullptr;
  int FrameNum = 0;
  const YUV4MPEG2 &Y4M;
};

int main(int argc, char *argv[]) {
  QApplication A(argc, argv);

  QFile F("/home/sean/tmp/foreman_cif.y4m");
  if (!F.open(QIODevice::ReadOnly)) {
    qDebug() << "Unable to open file";
    return 1;
  }
  const uchar *RawFile = F.map(0, F.size());
  if (!RawFile) {
    qDebug() << "Unable to map file";
    return 1;
  }
  YUV4MPEG2 Y4M{RawFile, (size_t)F.size()};

  TriangleWindow W{Y4M};
  W.resize(Y4M.Width, Y4M.Height);
  W.show();
  W.setAnimating(true);

  return A.exec();
}
