from datetime import datetime

print("-DBUILD_TIMESTAMP='\"%s\"'" % datetime.utcnow().strftime("%a, %d %b %Y %H:%M:%S GMT"))
