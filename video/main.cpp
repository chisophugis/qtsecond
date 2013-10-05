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

const char VertexShaderSource[] = R"(
attribute highp vec4 posAttr;
attribute lowp vec4 colAttr;
varying lowp vec4 col;
uniform highp mat4 matrix;
void main() {
   col = colAttr;
   gl_Position = matrix * posAttr;
}
)";
const char FragmentShaderSource[] = R"(
varying lowp vec4 col;
void main() {
   gl_FragColor = col;
}
)";

template <typename T> T *typedNullptr() { return static_cast<T *>(nullptr); }

template <typename MemberTy, typename StructTy>
GLvoid *offsetOfAsPtr(MemberTy StructTy::*MemberPtr) {
  return std::addressof(typedNullptr<StructTy>()->*MemberPtr);
}

struct Vertex {
  GLfloat XY[2];
  GLfloat RGB[3];
};

class TriangleWindow : public OpenGLWindow {
public:
  TriangleWindow() = default;

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
    ColAttr = Program->attributeLocation("colAttr");
    MatrixUniform = Program->uniformLocation("matrix");
  }
  void render() override {
    glViewport(0, 0, width(), height());

    glClearColor(0.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    Program->bind();

    QMatrix4x4 M;
    // M.ortho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    // M.perspective(60, static_cast<qreal>(width()) / height(), 0.1, 100.0);
    M.translate(0, UpDown, LeftRight);

    // M.translate(0, 0, -2);
    // M.rotate(100.0f * FrameNum / screen()->refreshRate(), 0, 1, 0);

    Program->setUniformValue(MatrixUniform, M);

    Vertex Vertices[] = {                     //
        {{-1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}}, //
        {{-1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},  //
        {{1.0f, -1.0f}, {0.0f, 0.0f, 1.0f}},  //
        {{1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}},   //
    };

    GLuint VBOID;
    glGenBuffers(1, &VBOID);
    glBindBuffer(GL_ARRAY_BUFFER, VBOID);

    glBufferData(GL_ARRAY_BUFFER, sizeof(Vertices), &Vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(PosAttr, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          offsetOfAsPtr(&Vertex::XY));
    glVertexAttribPointer(ColAttr, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          offsetOfAsPtr(&Vertex::RGB));

    glEnableVertexAttribArray(PosAttr);
    glEnableVertexAttribArray(ColAttr);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(ColAttr);
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
  GLuint PosAttr;
  GLuint ColAttr;
  GLuint MatrixUniform;

  QOpenGLShaderProgram *Program = nullptr;
  int FrameNum = 0;
};

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

template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args &&... args) {
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

std::unique_ptr<QImage> createImageFromMonochromeBytes(const uchar *Bytes,
                                                       int W, int H) {
  auto Ret = make_unique<QImage>(QSize{W, H}, QImage::Format_RGB888);
  uchar *RGBBuf = Ret->bits();
  for (int i = 0, e = W * H; i != e; ++i) {
    RGBBuf[3 * i] = Bytes[i];
    RGBBuf[3 * i + 1] = Bytes[i];
    RGBBuf[3 * i + 2] = Bytes[i];
  }
  return Ret;
}

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
  const uchar *FirstFrameLuma = skipToAfterNewline(skipToAfterNewline(RawFile));
  auto Image = createImageFromMonochromeBytes(FirstFrameLuma, 352, 288);
  QLabel L;
  L.setPixmap(QPixmap::fromImage(*Image));
  L.show();

  QSurfaceFormat Format;
  Format.setSamples(4);
  TriangleWindow W;
  W.setFormat(Format);
  W.resize(640, 480);
  W.show();
  W.setAnimating(true);

  return A.exec();
}
