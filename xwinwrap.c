/*
 * Copyright © 2005 Novell, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of
 * Novell, Inc. not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior permission.
 * Novell, Inc. makes no representations about the suitability of this
 * software for any purpose. It is provided "as is" without express or
 * implied warranty.
 *
 * NOVELL, INC. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL NOVELL, INC. BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: David Reveman <davidr@novell.com>
 */

/*
 * Modified by: Shantanu Goel
 * Tech Blog: http://tech.shantanugoel.com
 * Blog: http://blog.shantanugoel.com
 * Home Page: http://tech.shantanugoel.com/projects/linux/shantz-xwinwrap
 *
 * Changelog:
 * 15-Jan-09:   1. Fixed the bug where XFetchName returning a NULL for "name"
 *                 resulted in a crash.
 *              2. Provided an option to specify the desktop window name.
 *              3. Added debug messages
 *
 * 24-Aug-08:   1. Fixed the geometry option (-g) so that it works
 *              2. Added override option (-ov), for seamless integration with
 *                 desktop like a background in non-fullscreen modes
 *              3. Added shape option (-sh), to create non-rectangular windows.
 *                 Currently supporting circlular and triangular windows
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xproto.h>

#include <X11/extensions/shape.h>
#include <X11/extensions/Xrender.h>

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>

#define WIDTH  512
#define HEIGHT 384

#define OPAQUE 0xffffffff

#define NAME "xwinwrap"
#define VERSION "0.3"

#define DESKTOP_WINDOW_NAME_MAX_SIZE 25
#define DEFAULT_DESKTOP_WINDOW_NAME "Desktop"

#define DEBUG_MSG(x) if(debug) { fprintf(stderr, x); }

typedef enum
{
    SHAPE_RECT = 0,
    SHAPE_CIRCLE,
    SHAPE_TRIG,
} win_shape;

static pid_t pid = 0;

static char **childArgv = 0;
static int  nChildArgv  = 0;
char desktop_window_name[DESKTOP_WINDOW_NAME_MAX_SIZE];
int debug = 0;

    static int
addArguments (char **argv,
        int  n)
{
    char **newArgv;
    int  i;

    newArgv = realloc (childArgv, sizeof (char *) * (nChildArgv + n));
    if (!newArgv)
        return 0;

    for (i = 0; i < n; i++)
        newArgv[nChildArgv + i] = argv[i];

    childArgv   = newArgv;
    nChildArgv += n;

    return n;
}

    static void
setWindowOpacity (Display      *dpy,
        Window       win,
        unsigned int opacity)
{
    CARD32 o;

    o = opacity;

    XChangeProperty (dpy, win, XInternAtom (dpy, "_NET_WM_WINDOW_OPACITY", 0),
            XA_CARDINAL, 32, PropModeReplace,
            (unsigned char *) &o, 1);
}

    static Visual *
findArgbVisual (Display *dpy, int scr)
{
    XVisualInfo		*xvi;
    XVisualInfo		template;
    int			nvi;
    int			i;
    XRenderPictFormat	*format;
    Visual		*visual;

    template.screen = scr;
    template.depth  = 32;
    template.class  = TrueColor;

    xvi = XGetVisualInfo (dpy,
            VisualScreenMask |
            VisualDepthMask  |
            VisualClassMask,
            &template,
            &nvi);
    if (!xvi)
        return 0;

    visual = 0;
    for (i = 0; i < nvi; i++)
    {
        format = XRenderFindVisualFormat (dpy, xvi[i].visual);
        if (format->type == PictTypeDirect && format->direct.alphaMask)
        {
            visual = xvi[i].visual;
            break;
        }
    }

    XFree (xvi);

    return visual;
}

    static void
sigHandler (int sig)
{
    kill (pid, sig);
}

    static void
usage (void)
{
    fprintf(stderr, "%s v%s- Modified by Shantanu Goel. Visit http://tech.shantanugoel.com for updates, queries and feature requests\n", NAME, VERSION);
    fprintf (stderr, "\nUsage: %s [-g {w}x{h}+{x}+{y}] [-ni] [-argb] [-fs] [-s] [-st] [-sp] [-a] "
            "[-b] [-nf] [-o OPACITY] [-sh SHAPE] [-ov]-- COMMAND ARG1...\n", NAME);
    fprintf (stderr, "Options:\n \
            -g      - Specify Geometry (w=width, h=height, x=x-coord, y=y-coord. ex: -g 640x480+100+100)\n \
            -ni     - Ignore Input\n \
            -d      - Desktop Window Hack. Provide name of the \"Desktop\" window as parameter \
            -argb   - RGB\n \
            -fs     - Full Screen\n \
            -s      - Sticky\n \
            -st     - Skip Taskbar\n \
            -sp     - Skip Pager\n \
            -a      - Above\n \
            -b      - Below\n \
            -nf     - No Focus\n \
            -o      - Opacity value between 0 to 1 (ex: -o 0.20)\n \
            -sh     - Shape of window (choose between rectangle, circle or triangle. Default is rectangle)\n \
            -ov     - Set override_redirect flag (For seamless desktop background integration in non-fullscreenmode)\n \
            -debug  - Enable debug messages\n");
}

static Window find_desktop_window(Display *display, int screen, Window *root, Window *p_desktop)
{
    int i;
    unsigned int n;
    Window win = *root;
    Window troot, parent, *children;
    char *name;
    int status;
    int width  = DisplayWidth (display, screen);
    int height = DisplayHeight (display, screen);
    XWindowAttributes attrs;

    XQueryTree(display, *root, &troot, &parent, &children, &n);
    for (i = 0; i < (int) n; i++) 
    {
        status = XFetchName(display, children[i], &name);
        status |= XGetWindowAttributes(display, children[i], &attrs);
        if ((status != 0) && (NULL != name))
        {
            if( (attrs.map_state != 0) && (attrs.width == width) &&
                    (attrs.height == height) && (!strcmp(name, desktop_window_name)) )
            {
                //DEBUG_MSG("Found Window:%s\n", name);
                win = children[i];
                XFree(children);
                XFree(name);
                *p_desktop = win;
                return win;
            }
            if(name)
            {
                XFree(name);
            }
        }
    }
    DEBUG_MSG("Desktop Window Not found\n");
    return 0;
}

    int
main (int argc, char **argv)
{
    Display	    *dpy;
    Window	    win;
    Window	    root;
    Window      p_desktop = 0;
    int		    screen;
    XSizeHints	xsh;
    XWMHints	xwmh;
    char	    widArg[256];
    char	    *widArgv[] = { widArg };
    char	    *endArg = NULL;
    int		    i;
    int		    status = 0;
    unsigned int opacity = OPAQUE;
    int		    x = 0;
    int		    y = 0;
    unsigned int width = WIDTH;
    unsigned int height = HEIGHT;
    int		    argb = 0;
    int		    fullscreen = 0;
    int		    noInput = 0;
    int		    noFocus = 0;
    Atom	    state[256];
    int		    nState = 0;
    int         override = 0;
    win_shape   shape = SHAPE_RECT;
    Pixmap      mask;
    GC          mask_gc;
    XGCValues   xgcv;

    dpy = XOpenDisplay (NULL);
    if (!dpy)
    {
        fprintf (stderr, "%s: Error: couldn't open display\n", argv[0]);
        return 1;
    }

    screen = DefaultScreen (dpy);
    root   = RootWindow (dpy, screen);
    strcpy(desktop_window_name, DEFAULT_DESKTOP_WINDOW_NAME);

    for (i = 1; i < argc; i++)
    {
        if (strcmp (argv[i], "-g") == 0)
        {
            if (++i < argc)
                XParseGeometry (argv[i], &x, &y, &width, &height);
        }
        else if (strcmp (argv[i], "-ni") == 0)
        {
            noInput = 1;
        }
        else if (strcmp (argv[i], "-d") == 0)
        {
            ++i;
            strcpy(desktop_window_name, argv[i]);
        }
        else if (strcmp (argv[i], "-argb") == 0)
        {
            argb = 1;
        }
        else if (strcmp (argv[i], "-fs") == 0)
        {
            state[nState++] = XInternAtom (dpy, "_NET_WM_STATE_FULLSCREEN", 0);
            fullscreen = 1;
        }
        else if (strcmp (argv[i], "-s") == 0)
        {
            state[nState++] = XInternAtom (dpy, "_NET_WM_STATE_STICKY", 0);
        }
        else if (strcmp (argv[i], "-st") == 0)
        {
            state[nState++] = XInternAtom (dpy, "_NET_WM_STATE_SKIP_TASKBAR", 0);
        }
        else if (strcmp (argv[i], "-sp") == 0)
        {
            state[nState++] = XInternAtom (dpy, "_NET_WM_STATE_SKIP_PAGER", 0);
        }
        else if (strcmp (argv[i], "-a") == 0)
        {
            state[nState++] = XInternAtom (dpy, "_NET_WM_STATE_ABOVE", 0);
        }
        else if (strcmp (argv[i], "-b") == 0)
        {
            state[nState++] = XInternAtom (dpy, "_NET_WM_STATE_BELOW", 0);
        }
        else if (strcmp (argv[i], "-nf") == 0)
        {
            noFocus = 1;
        }
        else if (strcmp (argv[i], "-o") == 0)
        {
            if (++i < argc)
                opacity = (unsigned int) (atof (argv[i]) * OPAQUE);
        }
        else if (strcmp (argv[i], "-sh") == 0)
        {
            if (++i < argc)
            {
                if(strcasecmp(argv[i], "circle") == 0)
                {
                    shape = SHAPE_CIRCLE;
                }
                else if(strcasecmp(argv[i], "triangle") == 0)
                {
                    shape = SHAPE_TRIG;
                }
            }
        }
        else if (strcmp (argv[i], "-ov") == 0)
        {
            override = 1;
        }
        else if (strcmp (argv[i], "-debug") == 0)
        {
            debug = 1;
        }
        else if (strcmp (argv[i], "--") == 0)
        {
            break;
        }
        else
        {
            usage ();

            return 1;
        }
    }

    for (i = i + 1; i < argc; i++)
    {
        if (strcmp (argv[i], "WID") == 0)
            addArguments (widArgv, 1);
        else
            addArguments (&argv[i], 1);
    }

    if (!nChildArgv)
    {
        fprintf (stderr, "%s: Error: couldn't create command line\n", argv[0]);
        usage ();

        return 1;
    }

    addArguments (&endArg, 1);

    if (fullscreen)
    {
        xsh.flags  = PSize | PPosition;
        xsh.width  = DisplayWidth (dpy, screen);
        xsh.height = DisplayHeight (dpy, screen);
    }
    else
    {
        xsh.flags  = PSize;
        xsh.width  = width;
        xsh.height = height;
    }

    xwmh.flags = InputHint;
    xwmh.input = !noFocus;

    if (argb)
    {
        XSetWindowAttributes attr;
        Visual		     *visual;

        visual = findArgbVisual (dpy, screen);
        if (!visual)
        {
            fprintf (stderr, "%s: Error: couldn't find argb visual\n", argv[0]);
            return 1;
        }

        attr.background_pixel = 0;
        attr.border_pixel     = 0;
        attr.colormap	      = XCreateColormap (dpy, root, visual, AllocNone);

        win = XCreateWindow (dpy, root, 0, 0, xsh.width, xsh.height, 0,
                32, InputOutput, visual,
                CWBackPixel | CWBorderPixel | CWColormap, &attr);
    }
    else
    {
        XSetWindowAttributes attr;
        attr.override_redirect = override;

        if( override && find_desktop_window(dpy, screen, &root, &p_desktop) )
        {
            win = XCreateWindow (dpy, p_desktop, x, y, xsh.width, xsh.height, 0,
                    CopyFromParent, InputOutput, CopyFromParent,
                    CWOverrideRedirect, &attr);
        }
        else
        {
            win = XCreateWindow (dpy, root, x, y, xsh.width, xsh.height, 0,
                    CopyFromParent, InputOutput, CopyFromParent,
                    CWOverrideRedirect, &attr);
        }
    }

    XSetWMProperties (dpy, win, NULL, NULL, argv, argc, &xsh, &xwmh, NULL);

    if (opacity != OPAQUE)
        setWindowOpacity (dpy, win, opacity);

    if (noInput)
    {
        Region region;

        region = XCreateRegion ();
        if (region)
        {
            XShapeCombineRegion (dpy, win, ShapeInput, 0, 0, region, ShapeSet);
            XDestroyRegion (region);
        }
    }

    if (nState)
        XChangeProperty (dpy, win, XInternAtom (dpy, "_NET_WM_STATE", 0),
                XA_ATOM, 32, PropModeReplace,
                (unsigned char *) state, nState);

    if (shape)
    {
        mask = XCreatePixmap(dpy, win, width, height, 1);
        mask_gc = XCreateGC(dpy, mask, 0, &xgcv);

        switch(shape)
        {
            //Nothing special to be done if it's a rectangle
            case SHAPE_CIRCLE:
                /* fill mask */
                XSetForeground(dpy, mask_gc, 0);
                XFillRectangle(dpy, mask, mask_gc, 0, 0, width, height);

                XSetForeground(dpy, mask_gc, 1);
                XFillArc(dpy, mask, mask_gc, 0, 0, width, height, 0, 23040);
                break;

            case SHAPE_TRIG:
                {
                    XPoint points[3] = { {0, height},
                        {width/2, 0},
                        {width, height} };

                    XSetForeground(dpy, mask_gc, 0);
                    XFillRectangle(dpy, mask, mask_gc, 0, 0, width, height);

                    XSetForeground(dpy, mask_gc, 1);
                    XFillPolygon(dpy, mask, mask_gc, points, 3, Complex, CoordModeOrigin);
                }

                break;

            default:
                break;

        }
        /* combine */
        XShapeCombineMask(dpy, win, ShapeBounding, 0, 0, mask, ShapeSet);
    }

    XMapWindow (dpy, win);

    if(p_desktop == 0)
    {
        XLowerWindow(dpy, win);
    }

    XSync (dpy, win);

    sprintf (widArg, "0x%x", (int) win);

    pid = fork ();

    switch (pid) {
        case -1:
            perror ("fork");
            return 1;
        case 0:
            execvp (childArgv[0], childArgv);
            perror (childArgv[0]);
            exit (2);
            break;
        default:
            break;
    }

    signal (SIGTERM, sigHandler);
    signal (SIGINT,  sigHandler);

    for (;;)
    {
        if (waitpid (pid, &status, 0) != -1)
        {
            if (WIFEXITED (status))
                fprintf (stderr, "%s died, exit status %d\n", childArgv[0],
                        WEXITSTATUS (status));

            break;
        }
    }

    XDestroyWindow (dpy, win);
    XCloseDisplay (dpy);

    return 0;
}
