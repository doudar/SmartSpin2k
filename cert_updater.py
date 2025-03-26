#!/usr/bin/env python3
"""
This script fetches the current CA certificate for raw.githubusercontent.com
and updates the cert.h file with it during compilation.

Similar to git_tag_macro.py, this script is run as part of the build process
to ensure the certificate is always up to date.

This script can be run in two ways:
1. Directly: python cert_updater.py
2. As a PlatformIO pre-script: extra_scripts = pre:cert_updater.py
"""

import os
import ssl
import socket
import datetime
import re
import urllib.request
from pathlib import Path
import sys

# Define constants
CERT_FILE_PATH = "include/cert.h"
GITHUB_HOST = "raw.githubusercontent.com"
GITHUB_PORT = 443
CERT_FILE_HEADER = """/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * Automatically updated CA certificate for raw.githubusercontent.com
 * Last updated: {date}
 */

#pragma once

// certificate for https://raw.githubusercontent.com
// {cert_description}, valid until {valid_until}, size: {cert_size} bytes
const char* rootCACertificate =
"""

def determine_root_ca_from_github():
    """
    Connect to raw.githubusercontent.com and determine the root CA used
    
    Returns a tuple (organization_name, common_name) of the root CA
    """
    try:
        print(f"Connecting to {GITHUB_HOST} to determine root CA...")
        
        # Create SSL context and connect
        context = ssl.create_default_context()
        with socket.create_connection((GITHUB_HOST, GITHUB_PORT), timeout=10) as sock:
            with context.wrap_socket(sock, server_hostname=GITHUB_HOST) as ssock:
                # Get certificate chain
                cert_der = ssock.getpeercert(binary_form=True)
                
                # Parse certificate to get issuer information
                from cryptography import x509
                from cryptography.hazmat.backends import default_backend
                
                cert = x509.load_der_x509_certificate(cert_der, default_backend())
                
                # Extract issuer information - this is the immediate CA, not root
                issuer = cert.issuer
                
                # We need to get the complete chain to find the root CA
                # Get current certificate in text form for comparison
                cert_pem = ssl.DER_cert_to_PEM_cert(cert_der)
                
                # Get certificate chain in text form
                chain = []
                for der_cert in context.get_ca_certs(binary_form=True):
                    pem_cert = ssl.DER_cert_to_PEM_cert(der_cert)
                    chain.append(pem_cert)
                
                print(f"Found {len(chain)} certificates in chain")
                
                # Extract root CA information (last in chain or self-signed)
                root_ca_org = None
                root_ca_cn = None
                
                for i, attr in enumerate(issuer):
                    attr_name = attr.oid._name
                    if attr_name == 'organizationName':
                        root_ca_org = attr.value
                        print(f"Root CA Organization: {root_ca_org}")
                    elif attr_name == 'commonName':
                        root_ca_cn = attr.value
                        print(f"Root CA Common Name: {root_ca_cn}")
                
                # If we couldn't extract from the cert object, fall back to standard method
                if not (root_ca_org and root_ca_cn):
                    cert_info = ssock.getpeercert()
                    issuer = dict(x[0] for x in cert_info['issuer'])
                    root_ca_org = issuer.get('organizationName', 'Unknown')
                    root_ca_cn = issuer.get('commonName', 'Unknown')
                    print(f"Root CA (fallback method): {root_ca_org} {root_ca_cn}")
                
                return root_ca_org, root_ca_cn
    
    except ImportError:
        try:
            # Fall back to simpler method without cryptography library
            context = ssl.create_default_context()
            with socket.create_connection((GITHUB_HOST, GITHUB_PORT), timeout=10) as sock:
                with context.wrap_socket(sock, server_hostname=GITHUB_HOST) as ssock:
                    cert_info = ssock.getpeercert()
                    issuer = dict(x[0] for x in cert_info['issuer'])
                    org = issuer.get('organizationName', 'Unknown')
                    cn = issuer.get('commonName', 'Unknown')
                    print(f"Root CA (simple method): {org} {cn}")
                    return org, cn
        except Exception as e:
            print(f"Error getting certificate info: {e}")
            return None, None
            
    except Exception as e:
        print(f"Error connecting to GitHub: {e}")
        return None, None

def extract_certificates_from_mozilla(target_org=None, target_cn=None):
    """
    Extract certificates from Mozilla's bundle matching the target organization and common name
    
    Args:
        target_org: Organization name to search for
        target_cn: Common name to search for
    
    Returns:
        A list of matching certificates, or all certificates if no target specified
    """
    try:
        cert_url = "https://curl.se/ca/cacert.pem"
        print(f"Downloading certificates from {cert_url}...")
        
        with urllib.request.urlopen(cert_url) as response:
            cert_bundle = response.read().decode('utf-8')
            
            # Find all certificates in the bundle
            print("Parsing certificate bundle...")
            all_certs = re.findall(
                r'(-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----)',
                cert_bundle, re.DOTALL
            )
            print(f"Found {len(all_certs)} certificates in bundle")
            
            # Extract certificates with their names from the bundle
            named_certs = []
            for cert in all_certs:
                # Try to extract the certificate name from comments in the bundle
                cert_start_pos = cert_bundle.find(cert)
                if cert_start_pos > 0:
                    # Look for certificate name in comments before certificate
                    name_search = cert_bundle[max(0, cert_start_pos-500):cert_start_pos]
                    name_match = re.search(r'^###\s+([^\n]+)', name_search, re.MULTILINE)
                    if name_match:
                        cert_name = name_match.group(1).strip()
                        named_certs.append((cert_name, cert))
                        
                        # If we have target information, check for matches
                        if target_org and target_cn:
                            if (target_org.lower() in cert_name.lower() and
                                target_cn.lower() in cert_name.lower()):
                                print(f"*** Found matching certificate: {cert_name} ***")
                                return [cert]
                        elif target_org:
                            if target_org.lower() in cert_name.lower():
                                print(f"*** Found certificate with matching org: {cert_name} ***")
                                return [cert]
            
            # If we have specific targets but didn't find a match by name, search in cert contents
            if target_org or target_cn:
                print(f"Searching certificate contents for {target_org} {target_cn}...")
                matches = []
                for cert in all_certs:
                    match_org = target_org and re.search(re.escape(target_org), cert, re.IGNORECASE)
                    match_cn = target_cn and re.search(re.escape(target_cn), cert, re.IGNORECASE)
                    
                    if (target_org and target_cn and match_org and match_cn) or \
                       (target_org and not target_cn and match_org) or \
                       (target_cn and not target_org and match_cn):
                        print(f"Found certificate containing target strings")
                        matches.append(cert)
                
                if matches:
                    return matches
            
            # If still no matches or no target specified, return either named certs or all certs
            if named_certs:
                return [cert for _, cert in named_certs]
            else:
                print("Returning all certificates for testing")
                return all_certs
            
    except Exception as e:
        print(f"Warning: Certificate bundle fetch failed: {e}")
    
    return []

def test_certificate(cert_text, host=GITHUB_HOST, port=GITHUB_PORT):
    """
    Test if a certificate works for connecting to the specified host
    
    Returns a tuple of (success, error_message)
    """
    import tempfile
    
    # Create temporary certificate file
    with tempfile.NamedTemporaryFile(delete=False) as cert_file:
        cert_file.write(cert_text.encode('utf-8'))
        cert_file_path = cert_file.name
    
    try:
        # Create SSL context using the certificate
        context = ssl.create_default_context(cafile=cert_file_path)
        with socket.create_connection((host, port), timeout=10) as sock:
            with context.wrap_socket(sock, server_hostname=host) as ssock:
                # Get certificate info for validation
                cert_info = ssock.getpeercert()
                valid_until = datetime.datetime.strptime(
                    cert_info['notAfter'], '%b %d %H:%M:%S %Y %Z'
                ).strftime('%a %b %d %Y')
                
                issuer = dict(x[0] for x in cert_info['issuer'])
                cert_description = issuer.get('organizationName', 'Unknown')
                if 'commonName' in issuer:
                    cert_description = f"{cert_description} {issuer['commonName']}"
                
                os.unlink(cert_file_path)  # Clean up
                return True, (cert_description, valid_until)
                
    except Exception as e:
        # Clean up and return the error
        if os.path.exists(cert_file_path):
            os.unlink(cert_file_path)
        return False, str(e)

def get_certificate():
    """
    Fetch and test the appropriate root certificate for raw.githubusercontent.com
    
    This function first determines which root CA is used by GitHub, then gets that
    certificate from Mozilla's bundle and tests it
    """
    # First, determine the root CA that GitHub is using
    root_ca_org, root_ca_cn = determine_root_ca_from_github()
    
    if not root_ca_org and not root_ca_cn:
        print("Error: Could not determine root CA for GitHub")
        return None, None, None
    
    print(f"Fetching {root_ca_org} {root_ca_cn} certificate for {GITHUB_HOST}...")
    
    # Get matching certificates from Mozilla's bundle
    matching_certs = extract_certificates_from_mozilla(target_org=root_ca_org, target_cn=root_ca_cn)
    
    if not matching_certs:
        print(f"Error: Could not find certificate for {root_ca_org} {root_ca_cn}")
        return None, None, None
    
    # Try each certificate until we find one that works
    for i, cert in enumerate(matching_certs):
        print(f"Testing certificate {i+1}/{len(matching_certs)}...")
        success, result = test_certificate(cert)
        
        if success:
            cert_description, valid_until = result
            print(f"Certificate works: {cert_description}, valid until {valid_until}")
            return cert, cert_description, valid_until
        else:
            print(f"Certificate test {i+1} failed: {result}")
    
    # If we couldn't find a working certificate, try a fallback method with USERTrust
    # (keeping this for backward compatibility)
    print("Trying fallback method with USERTrust certificate...")
    usertrust_certs = extract_certificates_from_mozilla(target_org="USERTrust")
    
    for i, cert in enumerate(usertrust_certs):
        print(f"Testing USERTrust certificate {i+1}/{len(usertrust_certs)}...")
        success, result = test_certificate(cert)
        
        if success:
            cert_description, valid_until = result
            print(f"USERTrust certificate works: {cert_description}, valid until {valid_until}")
            return cert, cert_description, valid_until
        else:
            print(f"USERTrust certificate test {i+1} failed: {result}")
    
    print("All certificate tests failed")
    return None, None, None

def format_certificate(cert_text):
    """
    Format the certificate for inclusion in a C/C++ header file
    """
    lines = cert_text.strip().split('\n')
    formatted_lines = [f'    "{line}\\n"' for line in lines]
    return '\n'.join(formatted_lines)

def update_cert_file(cert_text, cert_description, valid_until, is_quiet=False):
    """
    Update the cert.h file with the new certificate
    
    Args:
        cert_text: The certificate text
        cert_description: Description of the certificate
        valid_until: Expiration date of the certificate
        is_quiet: If True, suppresses console output
    """
    if not cert_text:
        if not is_quiet:
            print("ERROR: No valid certificate found for raw.githubusercontent.com")
            cert_file = Path(CERT_FILE_PATH)
            if cert_file.exists():
                print(f"Keeping existing certificate in {CERT_FILE_PATH}")
                print("WARNING: The existing certificate may not work with current GitHub servers")
                print("         This could cause SSL verification errors during firmware updates")
            else:
                print(f"No existing {CERT_FILE_PATH} found")
                print("HTTPS connections to GitHub will fail until a valid certificate is installed")
        return False
    
    formatted_cert = format_certificate(cert_text)
    cert_size = len(cert_text)
    
    header = CERT_FILE_HEADER.format(
        date=datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        cert_description=cert_description or "Unknown Certificate",
        valid_until=valid_until or "Unknown",
        cert_size=cert_size
    )
    
    # Generate the new content
    new_content = f"{header}{formatted_cert};"
    
    # Write to the cert.h file
    cert_file = Path(CERT_FILE_PATH)
    
    # Only update if the certificate has actually changed
    if cert_file.exists():
        try:
            current_content = cert_file.read_text()
            if formatted_cert in current_content:
                if not is_quiet:
                    print("Certificate is up to date. No changes needed.")
                return True
        except Exception as e:
            if not is_quiet:
                print(f"Warning: Could not read current {CERT_FILE_PATH}: {e}")
                print("Will create a new certificate file.")
    
    try:
        # Ensure parent directory exists
        cert_file.parent.mkdir(parents=True, exist_ok=True)
        
        # Write the certificate
        cert_file.write_text(new_content)
        if not is_quiet:
            print(f"Certificate updated successfully in {CERT_FILE_PATH}")
            print(f"Certificate: {cert_description}, valid until {valid_until}")
        return True
    except Exception as e:
        if not is_quiet:
            print(f"Error writing to {CERT_FILE_PATH}: {e}")
        return False

def update_ca_certificate(is_quiet=False):
    """Update CA certificate for raw.githubusercontent.com"""
    if not is_quiet:
        print("Updating CA certificate for raw.githubusercontent.com...")
    
    cert_text, cert_description, valid_until = get_certificate()
    return update_cert_file(cert_text, cert_description, valid_until, is_quiet=is_quiet)

def log_to_stderr(message):
    """Print a message to stderr instead of stdout"""
    print(message, file=sys.stderr)

def main():
    """
    Main function when run directly or by PlatformIO build flags
    
    When called by PlatformIO in the build_flags section (like !python cert_updater.py),
    it needs to print a valid build flag to stdout without any other output.
    """
    # Save the original print function
    original_print = print
    
    # Override the print function in our modules to send everything to stderr
    def safe_print(*args, **kwargs):
        if 'file' not in kwargs:
            kwargs['file'] = sys.stderr
        original_print(*args, **kwargs)
    
    # Replace print in global scope
    builtins = sys.modules['builtins']
    setattr(builtins, 'print', safe_print)
    
    try:
        log_to_stderr("\nRunning cert_updater.py to update GitHub SSL certificate...")
        
        cert_file = Path(CERT_FILE_PATH)
        
        if cert_file.exists():
            log_to_stderr(f"Certificate file exists at {CERT_FILE_PATH}")
            log_to_stderr(f"File size: {cert_file.stat().st_size} bytes")
        else:
            log_to_stderr(f"Certificate file does not exist at {CERT_FILE_PATH}")
            log_to_stderr("Will create certificate file")
        
        # Update the certificate
        result = update_ca_certificate(is_quiet=False)
        
        if cert_file.exists():
            log_to_stderr(f"Certificate file updated successfully at {CERT_FILE_PATH}")
            log_to_stderr(f"File size: {cert_file.stat().st_size} bytes")
        else:
            log_to_stderr(f"WARNING: Certificate file could not be created at {CERT_FILE_PATH}")
        
        # Reset print back to original
        setattr(builtins, 'print', original_print)
        
        # Print only the build flag to stdout - this will be picked up by PlatformIO
        print("-DCERT_UPDATER_VERSION=1")
        
        return 0
    except Exception as e:
        log_to_stderr(f"ERROR in cert_updater.py: {e}")
        
        # Reset print back to original
        setattr(builtins, 'print', original_print)
        
        # Print only the build flag to stdout
        print("-DCERT_UPDATER_ERROR=1")
        return 1

if __name__ == "__main__":
    main()