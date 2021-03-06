#!/bin/bash
lcov --directory . --capture --output-file gdal.info 2>/dev/null
gcc filter_info.c -o filter_info
./filter_info < gdal.info > gdal_filtered.info 


#cp /usr/bin/genhtml .
# to remove date and occurences number, so that only real differences show up
#patch -p0 < genhtml.patch

genhtml -o ./coverage_html --ignore-errors source --num-spaces 2 gdal_filtered.info >/dev/null

openssl aes-256-cbc -K $encrypted_380b91a86902_key -iv $encrypted_380b91a86902_iv -in ../id_rsa-gdalautotest-coverage-results.enc -out ~/.ssh/id_rsa-gdalautotest-coverage-results -d
chmod 600 ~/.ssh/id_rsa-gdalautotest-coverage-results

echo "Host foo.github.com" >> ~/.ssh/config
echo "    StrictHostKeyChecking no" >> ~/.ssh/config
echo "    Hostname github.com" >> ~/.ssh/config
echo "    IdentityFile ~/.ssh/id_rsa-gdalautotest-coverage-results" >> ~/.ssh/config

mkdir gdalautotest-coverage-results
cd gdalautotest-coverage-results
git init
git config user.email "gdalbot@gdal.bot"
git config user.name "GDAL coveragebot"
git config push.default matching
git remote add origin git@foo.github.com:gdalautotest-coverage-results/gdalautotest-coverage-results.github.io.git
#git fetch
#git pull origin master
#rm -rf *
cp -r ../coverage_html .
echo "Results of coverage of GDAL autotest" > README.md
echo "See https://gdalautotest-coverage-results.github.io/coverage_html/index.html" >> README.md
git add -A > /dev/null
git commit -m "update with results of commit https://github.com/rouault/gdal_coverage/commit/$TRAVIS_COMMIT" >/dev/null
git push -f origin master
