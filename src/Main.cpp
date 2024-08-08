#include "Application.h"

int main()
{
    Application app;
    if (!app.onInit())
        return 1;

    while (app.isRunning())
    {
        app.onFrame();
    }

    app.onFinish();
    return 0;
}
