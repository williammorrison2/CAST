pipeline {
    agent any

    stages {
        stage('Build') {
            steps {
                echo 'Building..'
                #sh 'ssh wcmorris@c650f03p41-ug "/u/wcmorris/CI/build.sh"'
                #sh 'ssh root@c650mnp05-ug "/u/wcmorris/CI/install.sh"'
            }
        }
        stage('Test') {
            steps {
                echo 'Testing..'
                #sh 'ssh root@c650mnp05-ug "/test/results/clean_logs.sh"'
                #sh 'ssh root@c650mnp05-ug "/u/wcmorris/CAST/csmtest/tools/complete_fvt.sh"'
            }
        }
        stage('Deploy') {
            steps {
                echo 'Deploying....'
            }
        }
    }
    post {
        always {
            COMMIT = ${GIT_REVISION, length=10}
            DATE = sh 'echo 20$(date +%y-%m-%d)'
            PR = ${PULL_REQUEST}
            echo '${DATE}-PR-${PR}-${COMMIT}'
        }
    }
}
