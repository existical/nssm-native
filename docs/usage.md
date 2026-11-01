# Basic usage

Run these commands from the directory containing your built `nssm.exe`. Use an
elevated terminal when installing, removing or changing a Windows service.

```powershell
# Show version and architecture.
./nssm.exe version

# Install a service using the graphical installer.
./nssm.exe install MyService

# Install directly with an executable and its arguments.
./nssm.exe install MyService 'C:\Apps\worker.exe' --config 'C:\Apps\worker.ini'

# Start, inspect and stop it.
./nssm.exe start MyService
./nssm.exe status MyService
./nssm.exe stop MyService

# Open the graphical editor.
./nssm.exe edit MyService

# Remove the service, accepting the removal confirmation.
./nssm.exe remove MyService confirm
```

`nssm start` waits for the Service Control Manager to report `SERVICE_RUNNING`.
Its polling is limited to 60 seconds and can fail sooner if the service stops,
a status query fails, or a checkpoint stalls beyond its wait hint. A missing
wait hint gets a 10-second progress window; hints below one second get one
second. Changing only the hint does not count as checkpoint progress.
`AppThrottle` continues to control restart throttling and does not set this
command's polling deadline. Startup errors, including service-specific exit
codes, are reported with a nonzero command exit status.

A polling timeout does not stop the service; use `nssm status MyService` to check
its current state. `SERVICE_RUNNING` describes the Windows service state and
does not guarantee that the wrapped application's endpoints are ready.

NSSM supports application arguments, startup directories, service accounts,
environment settings, restart policies, output redirection, log rotation and
event hooks. The preserved [upstream manual](upstream/README.txt) contains the
complete command and configuration reference, along with author and contributor
credits. Its old compiler and Windows compatibility notes describe upstream
releases; see [current build requirements](building.md) for this continuation.
