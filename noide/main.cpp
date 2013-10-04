#include <QApplication>
#include <QGLWidget>
#include <QGLFunctions>
#include <QOpenGLShaderProgram>

#include <QTimer>

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

class MyGLWidget : public QGLWidget, protected QGLFunctions {
  Q_OBJECT

public:
  MyGLWidget() {
    QTimer *T = new QTimer();
    T->start(10);
    connect(T, SIGNAL(timeout()), this, SLOT(repaint()));
  }

protected:
  void initializeGL() override {
    initializeGLFunctions();
    Program = new QOpenGLShaderProgram(this);
    Program->addShaderFromSourceCode(QOpenGLShader::Vertex, VertexShaderSource);
    Program->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                     FragmentShaderSource);
    Program->link();
    PosAttr = Program->attributeLocation("posAttr");
    ColAttr = Program->attributeLocation("colAttr");
    MatrixUniform = Program->uniformLocation("matrix");
  }
  void paintGL() override {
    glClearColor(0.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    Program->bind();

    QMatrix4x4 M;
    M.perspective(60, 4.0 / 3.0, 0.1, 100.0);
    M.translate(0, 0, -2);
    // M.rotate(100.0f * FrameNum / screen()->refreshRate(), 0, 1, 0);
    M.rotate(100.0f * FrameNum / 60, 0, 1, 0);

    Program->setUniformValue(MatrixUniform, M);

    GLfloat Vertices[] = { //
      0.0f, 0.707f,        //
      -0.5f, -0.5f,        //
      0.5f, -0.5f          //
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
  void repaint() {
    paintGL();
  }
  QOpenGLShaderProgram *Program;
  GLuint PosAttr;
  GLuint ColAttr;
  GLuint MatrixUniform;
  int FrameNum = 0;
};
// <http://www.qtcentre.org/threads/28580-Why-does-qmake-moc-only-process-header-files>
#include "main.moc"

int main(int argc, char *argv[]) {
  QApplication a(argc, argv);
  MyGLWidget gl;
  gl.show();

  return a.exec();
}
