#ifndef OPENGLWINDOW_H
#define OPENGLWINDOW_H

#include <QOpenGLFunctions>
#include <QWindow>

class OpenGLWindow : public QWindow, protected QOpenGLFunctions {
  Q_OBJECT
public:
  explicit OpenGLWindow(QWindow *Parent = 0);
  ~OpenGLWindow();

  virtual void render();

  virtual void initialize();

  void setAnimating(bool Animating);

public
slots:
  void renderLater();
  void renderNow();

protected:
  bool event(QEvent *E) override;
  void exposeEvent(QExposeEvent *E) override;
  void resizeEvent(QResizeEvent *E) override;

private:
  bool UpdatePending;
  bool IsAnimating;

  QOpenGLContext *Context;
};

#endif // #ifndef OPENGLWINDOW_H
