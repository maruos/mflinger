pipeline {
  agent {
    dockerfile {
      filename 'Dockerfile'
    }
    
  }
  stages {
    stage('Build package') {
      steps {
        sh './scripts/build-package.sh'
      }
    }
  }
}