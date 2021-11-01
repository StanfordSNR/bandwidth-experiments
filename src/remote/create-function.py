#!/usr/bin/env python3

import os
import sys
import zipfile
import shutil
import argparse
import hashlib
import base64
import boto3

def create_function_package(output, program):
    PACKAGE_FILES = {
        "program": program,
        "main.py": os.path.join(os.path.dirname(__file__), "main.py"),
    }

    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as funczip:
        for fn, fp in PACKAGE_FILES.items():
            funczip.write(fp, fn)

def install_lambda_package(package_file, function_name, role, region, delete=False):
    with open(package_file, 'rb') as pfin:
        package_data = pfin.read()

    client = boto3.client('lambda', region_name=region)

    if delete:
        try:
            client.delete_function(FunctionName=function_name)
            print("Deleted function '{}'.".format(function_name))
        except:
            pass

    response = client.create_function(
        FunctionName=function_name,
        Runtime='python3.8',
        Role=role,
        Handler='main.handler',
        Code={
            'ZipFile': package_data
        },
        Timeout=900,
        MemorySize=3008
    )

    print("Created function '{}' ({}).".format(function_name, response['FunctionArn']))

    try:
        client.update_function_event_invoke_config(
                FunctionName=response['FunctionArn'],
                MaximumRetryAttempts=0,
                MaximumEventAgeInSeconds=60)
    except:
        client.put_function_event_invoke_config(
                FunctionName=response['FunctionArn'],
                MaximumRetryAttempts=0,
                MaximumEventAgeInSeconds=60)


    print("Invoke configuration for {} updated.".format(response['FunctionArn']))

def main():
    parser = argparse.ArgumentParser(description="Generate and install Lambda functions.")
    parser.add_argument('--delete', dest='delete', action='store_true', default=False)
    parser.add_argument('--role', dest='role', action='store', required=True)
    parser.add_argument('--region', dest='region', default=os.environ.get('AWS_REGION'), action='store')
    parser.add_argument('--name', dest='name', action='store', required=True)
    parser.add_argument('--program', dest='program', action='store', required=True)

    args = parser.parse_args()

    if not os.path.exists(args.program):
        raise Exception(f"Cannot find {args.progarm}")

    function_name = args.name
    function_file = "{}.zip".format(function_name)
    
    try:
        create_function_package(function_file, args.program)
        print("Installing lambda function {}... ".format(function_name), end='')
        install_lambda_package(function_file, function_name, args.role, args.region,
                               delete=args.delete)
    finally:
        os.remove(function_file)

if __name__ == '__main__':
    main()
