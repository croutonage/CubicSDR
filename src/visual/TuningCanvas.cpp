#include "TuningCanvas.h"

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#if !wxUSE_GLCANVAS
#error "OpenGL required: set wxUSE_GLCANVAS to 1 and rebuild the library"
#endif

#include "CubicSDR.h"
#include "CubicSDRDefs.h"
#include "AppFrame.h"
#include <algorithm>

wxBEGIN_EVENT_TABLE(TuningCanvas, wxGLCanvas) EVT_PAINT(TuningCanvas::OnPaint)
EVT_IDLE(TuningCanvas::OnIdle)
EVT_MOTION(TuningCanvas::OnMouseMoved)
EVT_LEFT_DOWN(TuningCanvas::OnMouseDown)
EVT_LEFT_UP(TuningCanvas::OnMouseReleased)
EVT_LEAVE_WINDOW(TuningCanvas::OnMouseLeftWindow)
EVT_ENTER_WINDOW(TuningCanvas::OnMouseEnterWindow)
EVT_MOUSEWHEEL(TuningCanvas::OnMouseWheelMoved)
wxEND_EVENT_TABLE()

TuningCanvas::TuningCanvas(wxWindow *parent, int *attribList) :
        InteractiveCanvas(parent, attribList), dragAccum(0), top(false), bottom(false), uxDown(0) {

    glContext = new TuningContext(this, &wxGetApp().GetContext(this));

    hoverIndex = 0;
    downIndex = 0;
    hoverState = TUNING_HOVER_NONE;
    downState = TUNING_HOVER_NONE;
    dragging = false;

    freqDP = -1.0;
    freqW = (1.0 / 3.0) * 2.0;

    bwDP = -1.0 + (2.25 / 3.0);
    bwW = (1.0 / 4.0) * 2.0;

    centerDP = -1.0 + (2.0 / 3.0) * 2.0;
    centerW = (1.0 / 3.0) * 2.0;
}

TuningCanvas::~TuningCanvas() {

}

void TuningCanvas::OnPaint(wxPaintEvent& WXUNUSED(event)) {
    wxPaintDC dc(this);
#ifdef __APPLE__    // force half-rate?
    glFinish();
#endif
    const wxSize ClientSize = GetClientSize();

    glContext->SetCurrent(*this);
    initGLExtensions();
    glViewport(0, 0, ClientSize.x, ClientSize.y);

    glContext->DrawBegin();

    DemodulatorInstance *activeDemod = wxGetApp().getDemodMgr().getLastActiveDemodulator();

    long long freq = 0;
    if (activeDemod != NULL) {
        freq = activeDemod->getFrequency();
    }
    long long bw = wxGetApp().getDemodMgr().getLastBandwidth();
    long long center = wxGetApp().getFrequency();

    if (mouseTracker.mouseDown()) {
        glContext->Draw(ThemeMgr::mgr.currentTheme->tuningBarDark.r, ThemeMgr::mgr.currentTheme->tuningBarDark.g, ThemeMgr::mgr.currentTheme->tuningBarDark.b,
                0.75, mouseTracker.getOriginMouseX(), mouseTracker.getMouseX());
    }

    RGBColor clr = top ? ThemeMgr::mgr.currentTheme->tuningBarUp : ThemeMgr::mgr.currentTheme->tuningBarDown;

    RGBColor clrDark = ThemeMgr::mgr.currentTheme->tuningBarDark;
    RGBColor clrMid = ThemeMgr::mgr.currentTheme->tuningBarLight;

    glContext->DrawTunerBarIndexed(1, 3, 11, freqDP, freqW, clrMid, 0.25, true, true); // freq
    glContext->DrawTunerBarIndexed(4, 6, 11, freqDP, freqW, clrDark, 0.25, true, true);
    glContext->DrawTunerBarIndexed(7, 9, 11, freqDP, freqW, clrMid, 0.25, true, true);
    glContext->DrawTunerBarIndexed(10, 11, 11, freqDP, freqW, clrDark, 0.25, true, true);

    glContext->DrawTunerBarIndexed(1, 3, 7, bwDP, bwW, clrMid, 0.25, true, true); // bw
    glContext->DrawTunerBarIndexed(4, 6, 7, bwDP, bwW, clrDark, 0.25, true, true);
    glContext->DrawTunerBarIndexed(7, 7, 7, bwDP, bwW, clrMid, 0.25, true, true);

    glContext->DrawTunerBarIndexed(1, 3, 11, centerDP, centerW, clrMid, 0.25, true, true); // freq
    glContext->DrawTunerBarIndexed(4, 6, 11, centerDP, centerW, clrDark, 0.25, true, true);
    glContext->DrawTunerBarIndexed(7, 9, 11, centerDP, centerW, clrMid, 0.25, true, true);
    glContext->DrawTunerBarIndexed(10, 11, 11, centerDP, centerW, clrDark, 0.25, true, true);

    if (hoverIndex > 0 && !mouseTracker.mouseDown()) {
        switch (hoverState) {

        case TUNING_HOVER_FREQ:
            glContext->DrawTunerBarIndexed(hoverIndex, hoverIndex, 11, freqDP, freqW, clr, 0.25, top, bottom); // freq
            break;
        case TUNING_HOVER_BW:
            glContext->DrawTunerBarIndexed(hoverIndex, hoverIndex, 7, bwDP, bwW, clr, 0.25, top, bottom); // bw
            break;
        case TUNING_HOVER_CENTER:
            glContext->DrawTunerBarIndexed(hoverIndex, hoverIndex, 11, centerDP, centerW, clr, 0.25, top, bottom); // center
            break;
        case TUNING_HOVER_NONE:
            break;
        }
    }

    glContext->DrawTuner(freq, 11, freqDP, freqW);
    glContext->DrawTuner(bw, 7, bwDP, bwW);
    glContext->DrawTuner(center, 11, centerDP, centerW);

    glContext->DrawEnd();

    SwapBuffers();
}

void TuningCanvas::StepTuner(ActiveState state, int exponent, bool up) {
    double exp = pow(10, exponent);
    long long amount = up?exp:-exp;

    DemodulatorInstance *activeDemod = wxGetApp().getDemodMgr().getLastActiveDemodulator();
    if (state == TUNING_HOVER_FREQ && activeDemod) {
        long long freq = activeDemod->getFrequency();
        long long diff = abs(wxGetApp().getFrequency() - freq);

        if (shiftDown) {
            bool carried = (long long)((freq) / (exp * 10)) != (long long)((freq + amount) / (exp * 10)) || (bottom && freq < exp);
            freq += carried?(9*-amount):amount;
        } else {
            freq += amount;
        }

        if (wxGetApp().getSampleRate() / 2 < diff) {
            wxGetApp().setFrequency(freq);
        }

        activeDemod->setFrequency(freq);
        activeDemod->updateLabel(freq);
        activeDemod->setFollow(true);
    }

    if (state == TUNING_HOVER_BW) {
        long bw = wxGetApp().getDemodMgr().getLastBandwidth();

        if (shiftDown) {
            bool carried = (long)((bw) / (exp * 10)) != (long)((bw + amount) / (exp * 10)) || (bottom && bw < exp);
            bw += carried?(9*-amount):amount;
        } else {
            bw += amount;
        }

        if (bw > wxGetApp().getSampleRate()) {
            bw = wxGetApp().getSampleRate();
        }

        wxGetApp().getDemodMgr().setLastBandwidth(bw);

        if (activeDemod) {
            activeDemod->setBandwidth(wxGetApp().getDemodMgr().getLastBandwidth());
        }
    }

    if (state == TUNING_HOVER_CENTER) {
        long long ctr = wxGetApp().getFrequency();
        if (shiftDown) {
            bool carried = (long long)((ctr) / (exp * 10)) != (long long)((ctr + amount) / (exp * 10)) || (bottom && ctr < exp);
            ctr += carried?(9*-amount):amount;
        } else {
            ctr += amount;
        }

        wxGetApp().setFrequency(ctr);
    }
}

void TuningCanvas::OnIdle(wxIdleEvent &event) {
    if (mouseTracker.mouseDown()) {
        if (downState != TUNING_HOVER_NONE) {
            dragAccum += 5.0*mouseTracker.getOriginDeltaMouseX();
            while (dragAccum > 1.0) {
                StepTuner(downState, downIndex-1, true);
                dragAccum -= 1.0;
                dragging = true;
            }
            while (dragAccum < -1.0) {
                StepTuner(downState, downIndex-1, false);
                dragAccum += 1.0;
                dragging = true;
            }
        } else {
            dragAccum = 0;
            dragging = false;
        }
    }

    Refresh(false);
}

void TuningCanvas::OnMouseMoved(wxMouseEvent& event) {
    InteractiveCanvas::OnMouseMoved(event);

    int index = 0;

    top = mouseTracker.getMouseY() >= 0.5;
    bottom = mouseTracker.getMouseY() <= 0.5;

    index = glContext->GetTunerDigitIndex(mouseTracker.getMouseX(), 11, freqDP, freqW); // freq
    if (index > 0) {
        hoverIndex = index;
        hoverState = TUNING_HOVER_FREQ;
    }

    if (!index) {
        index = glContext->GetTunerDigitIndex(mouseTracker.getMouseX(), 7, bwDP, bwW); // bw
        if (index > 0) {
            hoverIndex = index;
            hoverState = TUNING_HOVER_BW;
        }
    }

    if (!index) {
        index = glContext->GetTunerDigitIndex(mouseTracker.getMouseX(), 11, centerDP, centerW); // center
        if (index > 0) {
            hoverIndex = index;
            hoverState = TUNING_HOVER_CENTER;
        }
    }

    if (!index) {
        hoverIndex = 0;
        hoverState = TUNING_HOVER_NONE;
    } else {
        switch (hoverState) {
        case TUNING_HOVER_FREQ:
                setStatusText("Click or drag a digit to change frequency.  Hold shift to disable carry.");
            break;
        case TUNING_HOVER_BW:
                setStatusText("Click or drag a digit to change bandwidth.  Hold shift to disable carry.");
            break;
        case TUNING_HOVER_CENTER:
                setStatusText("Click or drag a digit to change center frequency.  Hold shift to disable carry.");
            break;
        }
    }


}

void TuningCanvas::OnMouseDown(wxMouseEvent& event) {
    InteractiveCanvas::OnMouseDown(event);

    uxDown = 2.0 * (mouseTracker.getMouseX() - 0.5);
    dragAccum = 0;

    mouseTracker.setVertDragLock(true);
    downIndex = hoverIndex;
    downState = hoverState;
}

void TuningCanvas::OnMouseWheelMoved(wxMouseEvent& event) {
    InteractiveCanvas::OnMouseWheelMoved(event);

    int hExponent = hoverIndex - 1;

    if (hoverState != TUNING_HOVER_NONE && !mouseTracker.mouseDown() && hoverIndex) {
        if (event.m_wheelAxis == wxMOUSE_WHEEL_VERTICAL) {
            StepTuner(hoverState, hExponent, (event.m_wheelRotation > 0)?true:false);
        } else {
            StepTuner(hoverState, hExponent, (event.m_wheelRotation < 0)?true:false);
        }
    }
}

void TuningCanvas::OnMouseReleased(wxMouseEvent& event) {
    GLint vp[4];
    glGetIntegerv( GL_VIEWPORT, vp);

    float viewHeight = (float) vp[3];
    float viewWidth = (float) vp[2];

    InteractiveCanvas::OnMouseReleased(event);

    int hExponent = hoverIndex - 1;

    if (hoverState != TUNING_HOVER_NONE && !dragging && (downState == hoverState) && (downIndex == hoverIndex)) {
        StepTuner(hoverState, hExponent, top);
    }

    mouseTracker.setVertDragLock(false);

    dragging = false;
    SetCursor(wxCURSOR_ARROW);
}

void TuningCanvas::OnMouseLeftWindow(wxMouseEvent& event) {
    InteractiveCanvas::OnMouseLeftWindow(event);
    SetCursor(wxCURSOR_CROSS);
    hoverIndex = 0;
    hoverState = TUNING_HOVER_NONE;
}

void TuningCanvas::OnMouseEnterWindow(wxMouseEvent& event) {
    InteractiveCanvas::mouseTracker.OnMouseEnterWindow(event);
    SetCursor(wxCURSOR_ARROW);
    hoverIndex = 0;
    hoverState = TUNING_HOVER_NONE;
}

void TuningCanvas::setHelpTip(std::string tip) {
    helpTip = tip;
}
