$ErrorActionPreference = "Continue"

$gz = Resolve-Path -Path $env:GITHUB_WORKSPACE\build\scendere_pow_server-*-win64.tar.gz

aws s3 cp $gz s3://repo.scendere.org/pow-server/scendere_pow_server-$env:TAG-win64.tar.gz --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
aws s3 cp $gz s3://repo.scendere.org/pow-server/scendere_pow_server-latest-win64.tar.gz --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers