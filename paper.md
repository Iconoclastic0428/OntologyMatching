# LSH Ontology Matching

## Authors

- **Shengqi Li**
  - UC San Diego
  - Address
  - Email: shl142@ucsd.edu

- **Kai Lin**
  - UC San Diego
  - Address
  - Email: klin@ucsd.edu

- **Amarnath Gupta**
  - UC San Diego
  - Address
  - Email: a1gupta@ucsd.edu

## Summary

Data similarity calculation is a foundational topic in data mining and machine learning, pivotal for tasks such as classification, clustering, regression, retrieval, and visualization. This computational concern is especially critical in ontology search, where finding similarity can significantly enhance the precision and relevance of search results and improve time performance. Classical measurements like cosine similarity, Hamming distance, and Jaccard similarity have been heavily utilized. The problem of computing these similarities is typically framed as the nearest neighbor (NN) problem, central to optimizing and refining search results within ontological datasets.

Recent advancements, such as the introduction of practical and optimal Locality-Sensitive Hashing (LSH) for angular distance [@andoni2015practical], have shown that efficient similarity computation can greatly improve performance in high-dimensional spaces. Ontology matching techniques leveraging word embeddings [@zhang2014ontology] and large-scale ontology alignment [@diallo2014effective] have demonstrated that integrating deep learning and knowledge rules [@khoudja2022deep] can significantly enhance matching accuracy. Additionally, methods like biomedical ontology matching using attention-based bidirectional long short-term memory networks [@hao2021medto], [@zhu2022integrating] have set new benchmarks for precision in complex datasets.

To address the challenges of efficiently finding similar data points within large ontological datasets, we developed the LSH Ontology Matching software. This tool is designed to provide an efficient, scalable solution by using hash functions to map similar items to the same buckets with high probability. This approach reduces the computational complexity associated with traditional similarity measures like cosine similarity and Jaccard similarity. The software constructs MinHash signatures for input data, which are then used in LSH to group similar items efficiently. By leveraging statistical techniques and high-performance computing, LSH Ontology Matching can handle large-scale datasets, making it accessible and useful for a wide range of applications. The software also includes tools for constructing and querying LSH mappings, providing users with an intuitive and powerful way to perform ontology matching and similarity searches, thereby enhancing the robustness and interpretability of the results.

## Statement of Need

Ontology matching is critical in data integration, semantic web, and information retrieval applications, where the goal is to identify semantically equivalent concepts across different ontologies [@shvaiko2011ontology]. Traditional similarity measures like cosine similarity and Jaccard similarity can be computationally intensive, especially with large datasets [@euzenat2007ontology]. Recent advancements in embedding techniques, such as triplet neural networks, have shown promise in improving the efficiency of ontology matching, but they still face scalability challenges [@zhu2022integrating], [@chen2021owl2vec]. To address these challenges, LSH Ontology Matching provides a scalable solution by leveraging LSH to perform approximate nearest neighbor searches efficiently [@cochez2014locality], [@duan2012instance]. This software fills a gap in existing tools by offering an open-source, high-performance solution for ontology matching tasks, making it accessible to researchers and practitioners across various domains [@xue2021biomedical].

## Usage

In the following, we use an example ontology from the FoodOn database and a set of recipes to demonstrate the utility of our LSH-based ontology matching system. We start by demonstrating how the data is loaded and preprocessed. Next, we apply the primary matching function to the data. Finally, we summarize the matched results and visualize them.

### Data Loading

FoodOn is a comprehensive ontology that aims to represent entities in the domain of food, from raw ingredients to processed products. It includes classes for various food items, their nutritional attributes, food products, and the relationships between these entities. The recipe dataset consists of a collection of recipes, each containing a list of ingredients. The task is to classify each food ingredient in the recipes to the corresponding entity in the FoodOn ontology. First, we use a Python script to load the ontology file with `owlready2`, process it, and generate a JSON file for the C++ code to process.

```
# load and process data
python3 ProcessOntology.py [ontology file] [output JSON file]

# look at item in the processed json file
file <- [output JSON file]
file[0]

"FOODON_03540746": ["scorzonera", "07460 - scorzonera (efsa foodex2)"]
```

The result can be interpreted as the name, processed label, and raw label.

### Data Processing

The primary processing function `EntityMatching::match` implements an efficient process to classify target data based on a given ontology. The function uses Locality-Sensitive Hashing (LSH) to perform approximate nearest neighbor searches, facilitating the matching of large-scale data using multithreading. To use `match()`, we need to specify the number of hash functions (`hash_func`) and bands (`bands`). For illustration, we use the default value `hash_func = 100` and `bands = 25`.

```
result <- match([ontologyPath] [targetPath] [outputPath] hash_func=100, bands=25)
print(result[0])

1026765,"Kiwi  Banana Shake '{""1 1/2 cups ice"" ""1 -2 kiwi (or more if you love kiwi)"" ""1 tablespoon sugar"" ""1 -2 banana (used for taste and thickening)""}'"
(FOODON_00004183, banana)
(FOODON_03414363, kiwi)
(FOODON_03420108, sugar)
```

### Visualization

To visualize our performance and its comparison to LexMapr, a ontological mapper. We illustrate the performance of our LSH-based ontology matching system compared to LexMapr in terms of precision, recall, processing time, and the number of unmatched items.
![comparison](./LexMapr%20Comparison.png)
The graph on the left shows the processing time in seconds for different band settings. There is a noticeable increase in time as the number of bands increases, with a sharp rise at 40 bands. This suggests a trade-off between accuracy and processing time. The graph on the right displays the number of matches that our system found but were not present in LexMapr's results. Our system consistently finds additional matches across different band settings, highlighting an improved performance in identifying relevant ontology matches.


The first graph shows the precision and recall of our system at different band settings for a threshold of 0.5.
![F1](./precision%20and%20recall.png)

### Conclusion
We developed an efficient LSH-based ontology matching system that significantly improves the classification of food ingredients into ontology entities, addressing limitations in existing methods such as LexMapr. This project demonstrates the enhanced precision and recall, as well as the ability to discover additional relevant matches, providing robust and accurate results. Our approach balances accuracy and processing time, ensuring practical applicability in large-scale data environments. Details of usage can be found at (https://github.com/Iconoclastic0428/OntologyMatching)