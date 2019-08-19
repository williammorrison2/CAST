pipeline {
    environment {
        COMMIT="${env.GIT_COMMIT.take(10)}"
        DATE = sh (script: 'echo 20$(date +%y-%m-%d)', returnStdout: true).trim()
        PR="${env.BRANCH_NAME}"
    }
    
    agent any
    options {
        lock resource: 'myResource'
    }
    stages {
        stage('Build') {
            steps {
                echo 'Building..'
                sh 'ssh wcmorris@c650f03p41-ug "/u/wcmorris/CI/build.sh"'
                //sh 'ssh root@c650mnp05-ug "/u/wcmorris/CI/install.sh"'
            }
        }
        stage('Test') {
            steps {
                echo 'Testing..'
                //sh 'ssh root@c650mnp05-ug "/test/results/clean_logs.sh"'
                //sh 'ssh root@c650mnp05-ug "/u/wcmorris/CAST/csmtest/tools/complete_fvt.sh"'
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
            //echo "${DATE}-${PR}-${COMMIT}"
            sh 'ssh root@c650mnp05-ug "/test/archive/archive_input.sh ${DATE}-${PR}-${COMMIT}"'
        }
    }
}
