#include "Application.h"

int main()
{
    Application app;

    if (!app.Initialize())
    {
        return 1;
    }

    while (app.IsRunning())
    {
        app.MainLoop();
    }

    app.Terminate();

    return 0;
}
