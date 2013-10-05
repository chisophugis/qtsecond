#include "openglwindow.h"
#include <QGuiApplication>
#include <QKeyEvent>
#include <QOpenGLShaderProgram>
#include <QScreen>
#include <Qt>

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
    //M.ortho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    //M.perspective(60, static_cast<qreal>(width()) / height(), 0.1, 100.0);
    M.translate(0, UpDown, LeftRight);

    //M.translate(0, 0, -2);
    //M.rotate(100.0f * FrameNum / screen()->refreshRate(), 0, 1, 0);

    Program->setUniformValue(MatrixUniform, M);

    Vertex Vertices[] = {                         //
      { { -1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } }, //
      { { -1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },  //
      { { 1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },  //
      { { 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f } },   //
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

int main(int argc, char *argv[]) {
  QGuiApplication a(argc, argv);

  QSurfaceFormat Format;
  Format.setSamples(4);
  TriangleWindow W;
  W.setFormat(Format);
  W.resize(640, 480);
  W.show();
  W.setAnimating(true);

  return a.exec();
}
