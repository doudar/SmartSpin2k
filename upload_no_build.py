Import("env")
env.AddCustomTarget(
    "uploadnobuild", 
    None, 
    'pio run -e %s -t nobuild -t upload' %
        env["PIOENV"], 
    title="Upload without building"
)