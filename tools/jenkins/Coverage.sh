#!/usr/bin/env sh

set -e

BUILDFOLDER=$WORKSPACE/build

die() {
    echo "$@"
    exit 1
}

do_cleanup() {
  for d in "$BUILDFOLDER"
  do
    if [ -d "$d" ]
    then
      rm -rf "$d" || die "Could not remote $d"
    fi
  done
}

### Check the node installation

for pkg in xsltproc gcovr ant cover2cover.py
do
   if command -v $pkg
   then
      echo "$pkg is installed. Good."
   else
      die "please install $pkg before proceeding"
   fi
done

### Cleanup previous runs

! [ -z "$WORKSPACE" ] || die "No WORKSPACE"
[ -d "$WORKSPACE" ] || die "WORKSPACE ($WORKSPACE) does not exist"

do_cleanup

for d in "$BUILDFOLDER"
do
  mkdir "$d" || die "Could not create $d"
done

NUMPROC="$(nproc)" || NUMPROC=1


cd $BUILDFOLDER
rm -rf java_cov*
rm -rf jacoco_cov*
rm -rf python_cov*
rm -rf xml_coverage.xml

ctest -D ExperimentalStart || true

cmake -Denable_documentation=OFF -Denable_lua=ON -Denable_java=ON \
      -Denable_compile_optimizations=OFF -Denable_compile_warnings=ON \
      -Denable_jedule=ON -Denable_mallocators=ON \
      -Denable_smpi=ON -Denable_smpi_MPICH3_testsuite=ON -Denable_model-checking=ON \
      -Denable_smpi_papi=ON \
      -Denable_memcheck=OFF -Denable_memcheck_xml=OFF -Denable_smpi_ISP_testsuite=ON -Denable_coverage=ON $WORKSPACE

#build with sonarqube scanner wrapper
/home/ci/build-wrapper-linux-x86/build-wrapper-linux-x86-64 --out-dir bw-outputs make -j$NUMPROC
JACOCO_PATH="/usr/local/share/jacoco"
export JAVA_TOOL_OPTIONS="-javaagent:${JACOCO_PATH}/lib/jacocoagent.jar"

export PYTHON_TOOL_OPTIONS="/usr/bin/python3-coverage run --parallel-mode --branch"

ctest --no-compress-output -D ExperimentalTest -j$NUMPROC || true
ctest -D ExperimentalCoverage || true

unset JAVA_TOOL_OPTIONS
if [ -f Testing/TAG ] ; then

  files=$( find . -size +1c -name "jacoco.exec" )
  i=0
  for file in $files
  do
    sourcepath=$( dirname $file )
    #convert jacoco reports in xml ones
    ant -f $WORKSPACE/tools/jenkins/jacoco.xml -Dexamplesrcdir=$WORKSPACE -Dbuilddir=$BUILDFOLDER/${sourcepath} -Djarfile=$BUILDFOLDER/simgrid.jar -Djacocodir=${JACOCO_PATH}/lib
    #convert jacoco xml reports in cobertura xml reports
    cover2cover.py $BUILDFOLDER/${sourcepath}/report.xml .. ../src/bindings/java src/bindings/java > $BUILDFOLDER/java_coverage_${i}.xml
    #save jacoco xml report as sonar only allows it 
    mv $BUILDFOLDER/${sourcepath}/report.xml $BUILDFOLDER/jacoco_coverage_${i}.xml
    i=$((i + 1))
  done

  #convert python coverage reports in xml ones
  cd $BUILDFOLDER
  find .. -size +1c -name ".coverage*" -exec mv {} . \;
  /usr/bin/python3-coverage combine
  /usr/bin/python3-coverage xml -i -o ./python_coverage.xml

   cd $WORKSPACE
   #convert all gcov reports to xml cobertura reports
   gcovr -r . --xml-pretty -e teshsuite -u -o $BUILDFOLDER/xml_coverage.xml
   xsltproc $WORKSPACE/tools/jenkins/ctest2junit.xsl build/Testing/$( head -n 1 < build/Testing/TAG )/Test.xml > CTestResults_memcheck.xml

   #generate sloccount report
   sloccount --duplicates --wide --details $WORKSPACE | grep -v -e '.git' -e 'mpich3-test' -e 'sloccount.sc' -e 'isp/umpire' -e 'build/' -e 'xml_coverage.xml' -e 'CTestResults_memcheck.xml' -e 'DynamicAnalysis.xml' > $WORKSPACE/sloccount.sc

   #upload files to codacy. CODACY_PROJECT_TOKEN must be setup !
   if ! [ -z $CODACY_PROJECT_TOKEN ]
   then 
     for report in $BUILDFOLDER/java_cov*
     do
       if [ ! -e "$report" ]; then continue; fi
       java -jar /home/ci/codacy-coverage-reporter-*-assembly.jar report -l Java -r $report --partial
     done
     java -jar /home/ci/codacy-coverage-reporter-*-assembly.jar final

     java -jar /home/ci/codacy-coverage-reporter-*-assembly.jar report -l Python -r $BUILDFOLDER/python_coverage.xml
     java -jar /home/ci/codacy-coverage-reporter-*-assembly.jar report -l C -f -r $BUILDFOLDER/xml_coverage.xml
     java -jar /home/ci/codacy-coverage-reporter-*-assembly.jar report -l CPP -f -r $BUILDFOLDER/xml_coverage.xml
   fi
fi
