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
    M.setToIdentity();

    Program->setUniformValue(MatrixUniform, M);

    GLfloat Vertices[] = {                                         //
      0.707f * TopVertexLeftRight, 0.707f * (TopVertexUpDown + 1), //
      -0.5f, -0.5f,                                                //
      0.5f, -0.5f                                                  //
    };
    GLfloat Colors[] = { //
      1.0f, 0.0f, 0.0f,  //
      0.0f, 1.0f, 0.0f,  //
      0.0f, 0.0f, 1.0f   //
    };

    glVertexAttribPointer(PosAttr, 2, GL_FLOAT, GL_FALSE, 0, Vertices);
    glVertexAttribPointer(ColAttr, 3, GL_FLOAT, GL_FALSE, 0, Colors);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(0);

    Program->release();
    ++FrameNum;
  }

private:
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
