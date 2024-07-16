# OntologyMatching

## Overview

The Ontology Matching Project uses Locality-Sensitive Hashing (LSH) with MinHash to perform a hash-join-like structure. This approach speeds up the filtering process in ontology comparison, ensuring that only records with a certain level of similarity are compared. This method enhances both the speed and precision of ontology matching compared to traditional methods like LexMapr.

## Prerequisites

````
Make
````

````
Python 3
````

##Usage

First, use

````
make
````

make to compile the project.

Then, use 

````
python3 ./ProcessOntology.py [ontology file] [output file]
````

To proecess your ontology. [ontology file] to the input ontology. The ontology file should be in a structured format in OWL.
[output file] is the path to the output file where the processed ontology will be stored. The output will be a json file with modified ontology records.

Then use 

````
./EntityMatching [path_to_ontology] [path_to_candidates] [path_to_output]
````

to perform the ontology matching. 

## Configuration

To improve the precision of the ontology matching process, you can configure custom stop words. This helps in filtering out unrelated words, allowing the program to focus on relevant terms.

In `ProcessOntology.py`:

Modify the word_list variable to include specific stop words for your domain.

In `main.cpp`:

Modify the word_set variable to include specific stop words for your domain.