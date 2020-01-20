# Automatic PR merge using Mergify - PoC Findings
Mergify is a GitHub Application that can be used to define rules as code for automatic pull request merging. 

## Installation
Full installation steps are covered in Mergify's own documentation [found here.](https://doc.mergify.io/getting-started.html#installation) A quick summary of the steps can be found below:

1) Enable Mergify on your account/organization via the [Github Marketplace](https://github.com/marketplace/mergify) or their [dashboard.](https://dashboard.mergify.io/)
2) Select your payment plan (see [below](#Payment-Options) for further details) and install Mergify.
3) Select which repositories you want Mergify to have access to.\
You can select all of them in your account/organization or select specific repositories from a list.
4) Press the install button, you will then be redirected to the Mergify dashboard.  
5) Create the configuration file that Mergify will use and merge it onto the default branch of your repository.\
This can be done from the Simulator tab on the Mergify dashboard. 

Once merged onto the default branch, the configuration file will apply Mergify rules to your pull request, and carry out actions on your behalf. 


## Configuration File 
**The configuration file should be created in the repositories root directory and named either .mergify.yml or .mergify/config.yml** 

The configuration file is made of a main key named pull_request_rules which contains a list of rules that dictates how pull requests will be actioned by Mergify.\
Each rule is made up of 3 elements:
* a name - describes what the rule does, it is not interpreted by Mergify and is used to help identify the rule and what it does.
* a list of conditions - each condition must match the for the rule to be applied.
* a list of actions - each action will be applied once all conditions are met.

Extensive documentation on configuring this file can be found [here.](https://doc.mergify.io/configuration.html) 

### Example Configuration Files
``` yml
# file: .mergify/config.yml 

# Simple merge rule for the master branch 
pull_request_rules: 
  - name: automatic merge when CI check 'pre-merge' passes and the PR has been approved by 1 or more reviewers. 
    conditions: 
      - "#approved-reviews-by>=1"
      - status-success=pre-merge
      - base=master 
    actions: 
      merge: 
        method: merge 
```
The example above shows a simple rule that will auto merge pull requests onto the master branch when the opened PR has passed a CI check and has received at least 1 approval from a reviewer. You can see Mergify using this example on this [PR.](https://github.com/muon-developers/abseil-cpp/pull/35/checks?check_run_id=399046157)
```yml
# file: .mergify/config.yml

# Alternative merge rule for the master branch 
pull_request_rules: 
  - name: automatic merge when GitHub branch protection passes 
    conditions:
      - base=master
    actions:
      merge: 
        method: merge 
```
The example above uses GitHubs branch protection rules to decide when to automatically merge onto the master branch. You can see Mergify using this example on this [PR.](https://github.com/muon-developers/abseil-cpp/pull/38/checks?check_run_id=399065091) \
The branch protection rules that were active when the PR was opened were:
* Required approving reviews=1
    * dismiss stale pull request approvals when new commits are pushed.
* require status checks to pass before merging: pre-merge
* Include administrators
* Restrict who can push to matching branches: mergify 

## Payment options

| Open Source | Pro | Enterprise | 
|-------------|-----|------------|
|Free | $4 per month per user | Monthly cost unknown, contact Mergify to find out more | 
| All Features | All Features | All Features |
| Community Support | Priority Support | Support plan unknown, assumed to be equivalent of Pro, contact Mergify to find out more. | 
| Public repositories only | Private repositories | Assumed to be equivalent of Pro, contact Mergify to find out more. |
| No On-boarding | Concierge On-boarding | Assumed to be equivalent of Pro, contact Mergify to find out more. | 
| | 14-day free trial available | | 

Contact details and more information available [here.](https://mergify.io/pricing)