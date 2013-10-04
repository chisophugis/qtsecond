#include "openglwindow.h"
#include <QCoreApplication>

OpenGLWindow::OpenGLWindow(QWindow *Parent)
    : QWindow(Parent), UpdatePending(false), IsAnimating(false), Context(0) {
  setSurfaceType(QWindow::OpenGLSurface);
}

OpenGLWindow::~OpenGLWindow() {}

void OpenGLWindow::initialize() {}

void OpenGLWindow::render() {
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void OpenGLWindow::renderLater() {
  if (UpdatePending)
    return;
  UpdatePending = true;
  QCoreApplication::postEvent(this, new QEvent(QEvent::UpdateRequest));
}

bool OpenGLWindow::event(QEvent *E) {
  switch (E->type()) {
  case QEvent::UpdateRequest:
    renderNow();
    return true;
  default:
    return QWindow::event(E);
  }
}

void OpenGLWindow::exposeEvent(QExposeEvent *E) {
  Q_UNUSED(E);
  if (isExposed())
    renderNow();
}

void OpenGLWindow::resizeEvent(QResizeEvent *E) {
  Q_UNUSED(E);
  if (isExposed())
    renderNow();
}

void OpenGLWindow::renderNow() {
  if (!isExposed())
    return;
  UpdatePending = false;
  bool NeedsInitialize = false;
  if (!Context) {
    Context = new QOpenGLContext(this);
    Context->setFormat(requestedFormat());
    Context->create();
    NeedsInitialize = true;
  }

  Context->makeCurrent(this);

  if (NeedsInitialize) {
    initializeOpenGLFunctions();
    initialize(); // For the subclass.
  }

  render(); // For the subclass.

  Context->swapBuffers(this);

  if (IsAnimating)
    renderLater();
}

void OpenGLWindow::setAnimating(bool Animating) {
  IsAnimating = Animating;
  if (Animating)
    renderLater();
}
